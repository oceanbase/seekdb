import logging
import os

import dotenv

from src.integrations.embedding import generate_response as generate_embedding
from src.integrations.llm import generate_response as generate_llm_response
from src.prompt import QUERY_SYSTEM_PROMPT, QUERY_USER_PROMPT_TEMPLATE
from src.storage.oceanbase import get_or_create_client
from src.util import Answer

logger = logging.getLogger(__name__)

dotenv.load_dotenv()

# OceanBase connection parameters
OCEANBASE_URI = os.getenv("OCEANBASE_URI")
OCEANBASE_USER = os.getenv("OCEANBASE_USER")
OCEANBASE_PASSWORD = os.getenv("OCEANBASE_PASSWORD")
OCEANBASE_DBNAME = os.getenv("OCEANBASE_DBNAME")

# Table name for storing document chunks
TABLE_NAME = "rag_documents"

# Number of top results to retrieve
TOP_K = 5


def search(question: str) -> Answer:
    """
    Query the RAG system with a question and return an answer.

    Args:
        question: The question to answer

    Returns:
        Answer object containing the question, answer, filename, and page
    """
    answer = Answer(
        question=question,
    )

    logger.debug(f"Processing query: '{question[:100]}...'")

    try:
        # Generate embedding for the question
        logger.debug("Generating embedding for question...")
        question_embedding = generate_embedding(question)

        # Initialize OceanBase client
        client = get_or_create_client()

        # Build hybrid search query
        # First, build a simple full-text search query
        fts_query = {
            "bool": {
                "must": [
                    {
                        "query_string": {
                            "fields": ["content"],
                            "type": "best_fields",
                            "query": question,
                            "minimum_should_match": "30%",
                        }
                    }
                ],
            }
        }

        # Build the hybrid search request
        search_request = {
            "query": fts_query,
            "knn": {
                "field": "vector",
                "k": TOP_K * 2,  # Retrieve more candidates for filtering
                "num_candidates": TOP_K * 4,
                "query_vector": question_embedding,
                "filter": fts_query,
                "similarity": 0.3,  # Similarity threshold
            },
            "from": 0,
            "size": TOP_K,
        }

        # Perform hybrid search
        logger.debug(f"Performing hybrid search with TOP_K={TOP_K}...")
        search_results = client.search(index=TABLE_NAME, body=search_request)

        if not search_results or len(search_results) == 0:
            logger.warning(
                f"No search results found for question: '{question[:50]}...'"
            )
            answer.answer = "抱歉，我没有找到相关的信息来回答这个问题。"
            return answer

        logger.debug(f"Found {len(search_results)} search results")

        # Extract relevant context from search results
        contexts = []
        filenames = []
        pages = []

        for result in search_results[:TOP_K]:
            content = result.get("content", "")
            filename = result.get("filename", "")
            page = result.get("page", 0)

            if content:
                contexts.append(content)
                if filename:
                    filenames.append(filename)
                if page:
                    pages.append(page)

        if not contexts:
            logger.warning(
                f"No valid contexts extracted from search results for question: '{question[:50]}...'"
            )
            answer.answer = "抱歉，我没有找到相关的信息来回答这个问题。"
            return answer

        logger.debug(
            f"Extracted {len(contexts)} valid contexts, total length: {sum(len(c) for c in contexts)} chars"
        )

        # Combine contexts
        context_text = "\n\n".join(contexts)

        # Use the most common filename and page from results
        if filenames:
            answer.filename = max(set(filenames), key=filenames.count)
        if pages:
            answer.page = max(set(pages), key=pages.count)

        # Generate answer using LLM
        logger.debug(f"Generating answer using LLM model ...")
        prompt = QUERY_USER_PROMPT_TEMPLATE.format(
            context_text=context_text,
            question=question,
        )

        messages = [
            {
                "role": "system",
                "content": QUERY_SYSTEM_PROMPT,
            },
            {"role": "user", "content": prompt},
        ]

        answer.answer = generate_llm_response(messages)
        logger.debug(
            f"Query completed: question='{question[:50]}...', filename='{answer.filename}', page={answer.page}"
        )

    except Exception as e:
        logger.error(f"Error in query: {e}", exc_info=True)
        answer.answer = f"查询过程中发生错误：{str(e)}"

    return answer
