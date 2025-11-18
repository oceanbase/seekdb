"""Prompt templates and constants for the RAG system."""

from src.prompt.query import QUERY_SYSTEM_PROMPT, QUERY_USER_PROMPT_TEMPLATE
from src.prompt.vlm import VLM_IMAGE_EXTRACTION_PROMPT

__all__ = [
    "QUERY_SYSTEM_PROMPT",
    "QUERY_USER_PROMPT_TEMPLATE",
    "VLM_IMAGE_EXTRACTION_PROMPT",
]
