import logging
import os
import time
from typing import List

import dotenv
from openai import APIConnectionError, APIError, OpenAI, RateLimitError

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
embedding_client = OpenAI(
    api_key=OPENAI_API_KEY,
    base_url=OPENAI_BASE_URL,
)


def generate_response(
    embedding_text: str,
    model: str = "text-embedding-v4",
    max_retries: int = 3,
    initial_backoff: float = 1.0,
) -> List[float]:
    """
    Generate embeddings from text using OpenAI-compatible API with error handling and retry logic.

    Args:
        embedding_text: Text to generate embeddings for
        model: Model name to use for embedding generation (default: "text-embedding-3-small")
        max_retries: Maximum number of retry attempts
        initial_backoff: Initial backoff time in seconds (doubles on each retry)

    Returns:
        The embedding vector as a list of floats

    Raises:
        Exception: If the embedding request fails after all retries
        ValueError: If the response is empty or invalid
    """
    last_exception = None

    for attempt in range(max_retries + 1):
        try:
            response = embedding_client.embeddings.create(
                model=model,
                input=embedding_text,
            )

            if not response or not response.data or len(response.data) == 0:
                raise ValueError("Empty or invalid response from OpenAI API")

            embedding_vector = response.data[0].embedding
            if not embedding_vector or len(embedding_vector) == 0:
                raise ValueError("Empty embedding vector from API")

            return embedding_vector

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
                raise e
            else:
                logger.error(
                    f"Error after {max_retries + 1} attempts (type: {error_type}, status: {status_code}): {str(e)}"
                )

        except Exception as e:
            # For unexpected errors, don't retry
            logger.error(f"Unexpected error: {str(e)}")
            raise e

    # If we've exhausted all retries, raise the last exception
    if last_exception:
        raise last_exception
    else:
        raise Exception("Failed to generate embedding after retries")


if __name__ == "__main__":
    result = generate_response("Hello, world!")
    print(f"Embedding dimension: {len(result)}")
    print(f"First 5 values: {result[:5]}")
