import logging
import os
import time
from typing import List, Optional, Union

import dotenv
from openai import APIConnectionError, APIError, OpenAI, RateLimitError

from .image_utils import cleanup_temp_files, process_images_to_base64

dotenv.load_dotenv()

logger = logging.getLogger(__name__)

OPENAI_BASE_URL = os.getenv(
    "OPENAI_BASE_URL",
    "https://dashscope-intl.aliyuncs.com/compatible-mode/v1",
)

OPENAI_API_KEY = os.getenv(
    "OPENAI_API_KEY",
    "",
)

# Initialize OpenAI client
vlm_client = OpenAI(
    api_key=OPENAI_API_KEY,
    base_url=OPENAI_BASE_URL,
)


def generate_response(
    prompt: str,
    images: Optional[Union[str, List[str]]] = None,
    model: str = "qwen3-vl-flash",
    max_retries: int = 3,
    initial_backoff: float = 1.0,
) -> str:
    """
    Generate a response from the VLM (Vision Language Model) using OpenAI-compatible API
    with error handling and retry logic.

    Args:
        prompt: Text prompt for the model
        images: Optional image path(s) or base64 encoded image(s). Can be a single path/string or a list
        model: Model name to use for generation (default: "qwen-vl-plus")
        max_retries: Maximum number of retry attempts
        initial_backoff: Initial backoff time in seconds (doubles on each retry)

    Returns:
        The generated response content as a string

    Raises:
        Exception: If the API request fails after all retries
        ValueError: If the response is empty or invalid
    """
    last_exception = None

    # Store original image paths for potential compression on errors
    original_image_paths = []
    if images:
        if isinstance(images, str):
            images = [images]
        original_image_paths = [img if os.path.isfile(img) else None for img in images]

    # Process images - convert file paths to base64 if needed
    processed_images, temp_files_to_cleanup = process_images_to_base64(
        images if images else [], compress_for_retry=False
    )

    # Build messages in OpenAI format
    # OpenAI format: content is a list of content blocks
    content_blocks = [{"type": "text", "text": prompt}]

    # Add images to content blocks
    for img_base64 in processed_images:
        # OpenAI format: data:image/{format};base64,{base64_string}
        # We'll use PNG as default format since we normalize to PNG
        image_url = f"data:image/png;base64,{img_base64}"
        content_blocks.append({"type": "image_url", "image_url": {"url": image_url}})

    messages = [{"role": "user", "content": content_blocks}]

    # Track if we've already compressed images for retry
    images_compressed = False

    for attempt in range(max_retries + 1):
        try:
            response = vlm_client.chat.completions.create(
                model=model,
                messages=messages,
                temperature=0.2,
            )

            if not response.choices or not response.choices[0].message.content:
                raise ValueError("Empty response from API")

            content = response.choices[0].message.content
            if not content or len(content.strip()) == 0:
                raise ValueError("Empty response content from API")

            # Clean up temporary files before returning
            cleanup_temp_files(temp_files_to_cleanup)

            return content

        except RateLimitError as e:
            last_exception = e
            if attempt < max_retries:
                backoff_time = initial_backoff * (2**attempt)
                logger.warning(
                    f"Rate limit exceeded. Retrying in {backoff_time:.2f} seconds... "
                    f"(attempt {attempt + 1}/{max_retries + 1})"
                )
                time.sleep(backoff_time)
            else:
                logger.error(f"Rate limit error after {max_retries + 1} attempts")

        except APIConnectionError as e:
            last_exception = e
            if attempt < max_retries:
                backoff_time = initial_backoff * (2**attempt)
                logger.warning(
                    f"Connection error. Retrying in {backoff_time:.2f} seconds... "
                    f"(attempt {attempt + 1}/{max_retries + 1})"
                )
                time.sleep(backoff_time)
            else:
                logger.error(f"Connection error after {max_retries + 1} attempts")

        except APIError as e:
            last_exception = e
            error_str = str(e).lower()
            error_type = type(e).__name__

            # Check HTTP status code if available
            status_code = None
            if hasattr(e, "status_code"):
                status_code = e.status_code
            elif hasattr(e, "response") and hasattr(e.response, "status_code"):
                status_code = e.response.status_code

            # Check for non-retryable format errors
            format_error_keywords = [
                "unknown format",
                "unsupported format",
                "invalid format",
                "image format",
                "failed to process inputs",
            ]
            is_format_error = any(
                keyword in error_str for keyword in format_error_keywords
            )

            if is_format_error:
                # Format errors are not retryable
                logger.error(
                    f"Image format error from OpenAI API: {str(e)}. "
                    f"This is likely due to an unsupported or corrupted image format. "
                    f"Please check the image file(s)."
                )
                cleanup_temp_files(temp_files_to_cleanup)
                raise e

            # Check if it's a retryable error (5xx server errors)
            is_retryable = False
            if status_code is not None:
                if 500 <= status_code < 600:
                    is_retryable = True
                    logger.warning(
                        f"HTTP {status_code} error from OpenAI API: {str(e)}. "
                        f"This is a server error and will be retried."
                    )
            else:
                # Check error message for retryable keywords
                is_retryable = any(
                    keyword in error_str
                    for keyword in [
                        "connection",
                        "timeout",
                        "rate limit",
                        "server error",
                        "temporary",
                        "unavailable",
                        "502",
                        "503",
                        "504",
                        "bad gateway",
                        "service unavailable",
                        "gateway timeout",
                    ]
                )

            # Handle 502 errors specifically - try compressing images
            is_502_error = (
                status_code == 502 or "502" in error_str or "bad gateway" in error_str
            )

            if is_502_error and original_image_paths and not images_compressed:
                # 502 error might be due to large images, try compressing them
                logger.warning(
                    f"502 error detected (likely due to large image). "
                    f"Attempting to compress images and retry..."
                )
                try:
                    # Clean up previous temp files
                    cleanup_temp_files(temp_files_to_cleanup)

                    # Re-process images with compression
                    processed_images, temp_files_to_cleanup = process_images_to_base64(
                        [img for img in original_image_paths if img is not None],
                        compress_for_retry=True,
                    )

                    # Rebuild messages with compressed images
                    content_blocks = [{"type": "text", "text": prompt}]
                    for img_base64 in processed_images:
                        image_url = f"data:image/png;base64,{img_base64}"
                        content_blocks.append(
                            {"type": "image_url", "image_url": {"url": image_url}}
                        )
                    messages = [{"role": "user", "content": content_blocks}]

                    images_compressed = True
                    logger.info("Images compressed, retrying request...")
                    # Continue to retry without backoff delay for compression retry
                    continue
                except Exception as compress_error:
                    logger.error(
                        f"Failed to compress images for retry: {compress_error}. "
                        f"Will retry with original images."
                    )
                    # Fall through to normal retry logic

            if is_retryable and attempt < max_retries:
                backoff_time = initial_backoff * (2**attempt)
                logger.warning(
                    f"Error occurred (type: {error_type}, status: {status_code}): {str(e)}. "
                    f"Retrying in {backoff_time:.2f} seconds... "
                    f"(attempt {attempt + 1}/{max_retries + 1})"
                )
                time.sleep(backoff_time)
            elif not is_retryable:
                # Non-retryable errors should be raised immediately
                logger.error(
                    f"Non-retryable error (type: {error_type}, status: {status_code}): {str(e)}"
                )
                cleanup_temp_files(temp_files_to_cleanup)
                raise e
            else:
                logger.error(
                    f"Error after {max_retries + 1} attempts (type: {error_type}, status: {status_code}): {str(e)}"
                )

        except Exception as e:
            # For unexpected errors, don't retry
            cleanup_temp_files(temp_files_to_cleanup)
            raise e

    # Clean up temporary files before returning or raising
    cleanup_temp_files(temp_files_to_cleanup)

    # If we've exhausted all retries, raise the last exception
    if last_exception:
        raise last_exception
    else:
        raise Exception("Failed to generate response after retries")


if __name__ == "__main__":
    # Test with text only
    result = generate_response("What is a vision language model?")
    print(f"Text-only response: {result}")

    # Test with image (uncomment and provide image path)
    # result = generate_response(
    #     "Describe this image in detail.",
    #     images="path/to/image.jpg"
    # )
    # print(f"Image response: {result}")
