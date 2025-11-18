import glob
import logging
import os
import uuid
from concurrent.futures import ThreadPoolExecutor, as_completed
from typing import List

import dotenv
from pyobvector import VECTOR, FtsIndexParam, FtsParser, VectorIndex
from pyobvector.client.hybrid_search import HybridSearch
from sqlalchemy import VARCHAR, Column, Integer

logger = logging.getLogger(__name__)

try:
    from pypdf import PdfReader
except ImportError:
    try:
        from PyPDF2 import PdfReader
    except ImportError:
        PdfReader = None

from src.integrations.embedding import generate_response as generate_embedding
from src.integrations.vlm import generate_response as generate_vlm_response
from src.parser.pdf import compress_image, pdf_page_to_image
from src.prompt.vlm import VLM_IMAGE_EXTRACTION_PROMPT
from src.split.split import split_text
from src.storage.oceanbase import get_or_create_client

dotenv.load_dotenv()

# Embedding dimension from environment variable
EMBEDDING_DIM = 1024

# Table name for storing document chunks
TABLE_NAME = "rag_documents"


def create_table_if_not_exists(client: HybridSearch, vector_dim: int):
    """Create the documents table if it doesn't exist."""
    logger.info(f"Creating table '{TABLE_NAME}' with vector dimension {vector_dim}...")
    try:
        # Try to drop table if exists (for re-running)
        client.drop_table_if_exist(table_name=TABLE_NAME)
        logger.debug(f"Dropped existing table '{TABLE_NAME}' if it existed")
    except Exception:
        logger.debug(f"Table '{TABLE_NAME}' does not exist, will create new one")

    # Create table with required columns
    client.create_table(
        table_name=TABLE_NAME,
        columns=[
            Column("id", Integer, primary_key=True, autoincrement=True),
            Column("source_id", VARCHAR(64)),  # Unique identifier for each chunk
            Column("filename", VARCHAR(512)),  # PDF filename
            Column("page", Integer),  # Page number (1-indexed)
            Column("content", VARCHAR(8192)),  # Text content
            Column("vector", VECTOR(vector_dim)),  # Embedding vector
        ],
        indexes=[
            VectorIndex("vec_idx", "vector", params="distance=l2, type=hnsw, lib=vsag"),
        ],
        mysql_charset="utf8mb4",
        mysql_collate="utf8mb4_unicode_ci",
        mysql_organization="heap",
    )

    # Create full-text search index on content
    client.create_fts_idx_with_fts_index_param(
        table_name=TABLE_NAME,
        fts_idx_param=FtsIndexParam(
            index_name="fts_idx_content",
            field_names=["content"],
            parser_type=FtsParser.IK,
        ),
    )
    logger.info(
        f"Table '{TABLE_NAME}' created successfully with vector and FTS indexes"
    )


def process_pdf_file(
    pdf_file: str,
    client: HybridSearch,
) -> int:
    """
    Process a single PDF file with fixed workflow for each page:
    PDF to image -> VLM extraction -> Split -> Embed -> Store

    Args:
        pdf_file: Path to the PDF file
        client: OceanBase client for database operations

    Returns:
        Number of chunks inserted
    """
    filename = os.path.basename(pdf_file)
    logger.info(f"Processing {filename}...")

    try:
        # Get total number of pages from PDF
        try:
            if PdfReader is None:
                raise ImportError("PdfReader not available")
            reader = PdfReader(pdf_file)
            total_pages = len(reader.pages)
        except Exception as e:
            logger.error(f"Failed to read PDF {filename}: {e}")
            return 0

        logger.debug(f"  Total pages: {total_pages}")

        # Process each page with fixed workflow
        chunks_to_insert = []
        for page_num in range(1, total_pages + 1):
            page_image_path = None
            compressed_image_path = None
            page_text = ""

            try:
                # Step 1: Convert PDF to image
                page_image_path = pdf_page_to_image(pdf_file, page_num, dpi=100)
                logger.debug(f"Converted page {page_num} to image: {page_image_path}")

                # Step 2: Extract information using VLM
                try:
                    page_text = generate_vlm_response(
                        VLM_IMAGE_EXTRACTION_PROMPT, images=page_image_path
                    )
                    if page_text.strip():
                        logger.debug(f"Extracted text from page {page_num} using VLM")
                except Exception as e:
                    # Check if it's a 502 error (likely due to large image)
                    error_str = str(e).lower()
                    is_502_error = (
                        "502" in error_str
                        or "bad gateway" in error_str
                        or (hasattr(e, "status_code") and e.status_code == 502)
                    )

                    if is_502_error:
                        logger.warning(
                            f"502 error detected for page {page_num} of {filename}. "
                            f"Attempting to compress image and retry..."
                        )
                        try:
                            # Compress the image and retry
                            compressed_image_path = compress_image(
                                page_image_path,
                                max_size_mb=5.0,
                                max_dimension=2048,
                            )
                            page_text = generate_vlm_response(
                                VLM_IMAGE_EXTRACTION_PROMPT,
                                images=compressed_image_path,
                            )
                            if page_text.strip():
                                logger.info(
                                    f"Successfully extracted text from compressed page {page_num} image"
                                )
                        except Exception as retry_e:
                            logger.warning(
                                f"Failed to extract text from compressed page {page_num} image of {filename}: {retry_e}"
                            )
                    else:
                        logger.warning(
                            f"Failed to extract text from page {page_num} image of {filename}: {e}"
                        )
            except Exception as e:
                logger.warning(f"Failed to process page {page_num} of {filename}: {e}")
            finally:
                # Clean up temporary image files
                if compressed_image_path and os.path.exists(compressed_image_path):
                    try:
                        os.unlink(compressed_image_path)
                    except Exception as e:
                        logger.debug(
                            f"Failed to delete compressed image {compressed_image_path}: {e}"
                        )
                if page_image_path and os.path.exists(page_image_path):
                    try:
                        os.unlink(page_image_path)
                    except Exception as e:
                        logger.debug(
                            f"Failed to delete temporary page image {page_image_path}: {e}"
                        )

            # Skip if no content extracted
            if not page_text.strip():
                logger.debug(f"No content extracted from page {page_num}, skipping")
                continue

            # Step 3: Split text
            text_chunks = split_text(page_text)

            # Step 4 & 5: Embed -> Store
            for chunk_text in text_chunks:
                if not chunk_text.strip():
                    continue

                # Generate embedding
                try:
                    embedding = generate_embedding(chunk_text)
                except Exception as e:
                    logger.warning(
                        f"Failed to generate embedding for chunk in {filename} page {page_num}: {e}"
                    )
                    continue

                # Create unique source_id
                source_id = str(uuid.uuid4())

                chunks_to_insert.append(
                    {
                        "source_id": source_id,
                        "filename": filename,
                        "page": page_num,
                        "content": chunk_text[:8190],  # Truncate if too long
                        "vector": embedding,
                    }
                )

        # Batch insert chunks
        chunks_inserted = 0
        if chunks_to_insert:
            # Insert in batches to avoid memory issues
            batch_size = 100
            for i in range(0, len(chunks_to_insert), batch_size):
                batch = chunks_to_insert[i : i + batch_size]
                client.insert(table_name=TABLE_NAME, data=batch)
                chunks_inserted += len(batch)
                logger.debug(
                    f"Inserted {len(batch)} chunks from {filename} (total: {chunks_inserted})"
                )
            logger.info(
                f"Completed processing {filename}: inserted {chunks_inserted} chunks"
            )

        return chunks_inserted

    except Exception as e:
        logger.error(f"Error processing {filename}: {e}", exc_info=True)
        return 0


def add(dataset_dir: str, max_worker: int):
    """
    Prepare RAG data by parsing PDFs, generating embeddings, and storing in OceanBase.

    Args:
        dataset_dir: Directory containing PDF files
        max_worker: Number of parallel workers for processing PDF files (default: 2)
    """
    pdf_files: List[str] = glob.glob(os.path.join(dataset_dir, "*.pdf"))

    if not pdf_files:
        logger.warning(f"No PDF files found in {dataset_dir}")
        return

    logger.info(f"Found {len(pdf_files)} PDF files")

    # Initialize OceanBase client
    client = get_or_create_client()

    # Use embedding dimension from environment variable
    vector_dim = EMBEDDING_DIM
    logger.info(f"Using embedding vector dimension: {vector_dim}")

    create_table_if_not_exists(client, vector_dim)

    # Process PDF files in parallel
    total_chunks = 0
    with ThreadPoolExecutor(max_workers=max_worker) as executor:
        # Submit all PDF processing tasks
        future_to_pdf = {
            executor.submit(process_pdf_file, pdf_file, client): pdf_file
            for pdf_file in pdf_files
        }

        # Collect results as they complete
        for future in as_completed(future_to_pdf):
            pdf_file = future_to_pdf[future]
            try:
                chunks_inserted = future.result()
                total_chunks += chunks_inserted
            except Exception as e:
                filename = os.path.basename(pdf_file)
                logger.error(
                    f"Exception occurred while processing {filename}: {e}",
                    exc_info=True,
                )

    logger.info(f"Preparation complete! Total chunks inserted: {total_chunks}")
