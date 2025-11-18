import os
import time
from typing import Dict, List

import dotenv
from openai import APIConnectionError, APIError, OpenAI, RateLimitError

dotenv.load_dotenv()

OPENAI_BASE_URL = os.getenv(
    "OPENAI_BASE_URL",
    "https://dashscope-intl.aliyuncs.com/compatible-mode/v1",
)

OPENAI_API_KEY = os.getenv(
    "OPENAI_API_KEY",
    "",
)

llm = OpenAI(
    api_key=OPENAI_API_KEY,
    base_url=OPENAI_BASE_URL,
)


def generate_response(
    messages: List[Dict[str, str]],
    model="qwen-plus",
    max_retries: int = 3,
    initial_backoff: float = 1.0,
    timeout: float = 300.0,
) -> str:
    """
    Generate a response from the LLM with error handling and retry logic.

    Args:
        messages: List of message dictionaries with 'role' and 'content' keys
        model: Model name to use for generation
        max_retries: Maximum number of retry attempts
        initial_backoff: Initial backoff time in seconds (doubles on each retry)
        timeout: Request timeout in seconds (default: 300.0)

    Returns:
        The generated response content as a string

    Raises:
        APIError: If the API request fails after all retries
        ValueError: If the response is empty or invalid
        TimeoutError: If the request times out
    """
    last_exception = None

    for attempt in range(max_retries + 1):
        try:
            response = llm.chat.completions.create(
                model=model,
                messages=messages,
                temperature=0.2,
                timeout=timeout,
            )

            if not response.choices or not response.choices[0].message.content:
                raise ValueError("Empty response from API")

            return response.choices[0].message.content

        except RateLimitError as e:
            last_exception = e
            if attempt < max_retries:
                backoff_time = initial_backoff * (2**attempt)
                print(
                    f"Rate limit exceeded. Retrying in {backoff_time:.2f} seconds... (attempt {attempt + 1}/{max_retries + 1})"
                )
                time.sleep(backoff_time)
            else:
                print(f"Rate limit error after {max_retries + 1} attempts")

        except APIConnectionError as e:
            last_exception = e
            if attempt < max_retries:
                backoff_time = initial_backoff * (2**attempt)
                print(
                    f"Connection error. Retrying in {backoff_time:.2f} seconds... (attempt {attempt + 1}/{max_retries + 1})"
                )
                time.sleep(backoff_time)
            else:
                print(f"Connection error after {max_retries + 1} attempts")

        except TimeoutError as e:
            last_exception = e
            if attempt < max_retries:
                backoff_time = initial_backoff * (2**attempt)
                print(
                    f"Request timeout ({timeout}s). Retrying in {backoff_time:.2f} seconds... (attempt {attempt + 1}/{max_retries + 1})"
                )
                time.sleep(backoff_time)
            else:
                print(f"Request timeout after {max_retries + 1} attempts")

        except APIError as e:
            # For other API errors, check if they're retryable
            if e.status_code and e.status_code >= 500:
                # Server errors are retryable
                last_exception = e
                if attempt < max_retries:
                    backoff_time = initial_backoff * (2**attempt)
                    print(
                        f"Server error ({e.status_code}). Retrying in {backoff_time:.2f} seconds... (attempt {attempt + 1}/{max_retries + 1})"
                    )
                    time.sleep(backoff_time)
                else:
                    print(f"Server error after {max_retries + 1} attempts")
            else:
                # Client errors (4xx) are not retryable
                raise e

        except Exception as e:
            # For unexpected errors, don't retry
            raise e

    # If we've exhausted all retries, raise the last exception
    if last_exception:
        raise last_exception
    else:
        raise APIError("Failed to generate response after retries")


if __name__ == "__main__":
    response = generate_response(
        messages=[
            {"role": "system", "content": "You are a helpful assistant."},
            {"role": "user", "content": "Who are you?"},
        ]
    )

    print(response)
