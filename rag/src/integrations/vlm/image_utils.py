"""
Shared image processing utilities for VLM clients.
"""

import base64
import logging
import os
import tempfile
from typing import List, Tuple

# Try to import PIL for image format detection and conversion
try:
    from PIL import Image

    PIL_AVAILABLE = True
except ImportError:
    PIL_AVAILABLE = False

logger = logging.getLogger(__name__)


def compress_image_for_retry(image_path: str, compression_factor: float = 0.7) -> str:
    """
    Compress an image by reducing its resolution (DPI) for retry after 502 error.

    Args:
        image_path: Path to the image file to compress
        compression_factor: Factor to reduce dimensions (default: 0.7, meaning 70% of original)

    Returns:
        Path to the compressed temporary image file
    """
    if not PIL_AVAILABLE:
        logger.warning("PIL not available, cannot compress image")
        return image_path

    try:
        with Image.open(image_path) as img:
            # Get original dimensions
            original_width, original_height = img.size
            new_width = int(original_width * compression_factor)
            new_height = int(original_height * compression_factor)

            # Convert to RGB if necessary
            if img.mode in ("RGBA", "LA", "P"):
                background = Image.new("RGB", img.size, (255, 255, 255))
                if img.mode == "P":
                    img = img.convert("RGBA")
                background.paste(
                    img,
                    mask=(img.split()[-1] if img.mode == "RGBA" else None),
                )
                img = background
            elif img.mode != "RGB":
                img = img.convert("RGB")

            # Resize image
            img = img.resize((new_width, new_height), Image.Resampling.LANCZOS)

            # Save compressed image with lower quality
            temp_file = tempfile.NamedTemporaryFile(delete=False, suffix=".jpg")
            img.save(temp_file.name, "JPEG", quality=75, optimize=True)
            temp_file.close()

            original_size = os.path.getsize(image_path) / (1024 * 1024)
            compressed_size = os.path.getsize(temp_file.name) / (1024 * 1024)
            logger.info(
                f"Compressed image for retry: {original_size:.2f} MB -> {compressed_size:.2f} MB "
                f"(resolution: {original_width}x{original_height} -> {new_width}x{new_height})"
            )

            return temp_file.name
    except Exception as e:
        logger.warning(f"Failed to compress image {image_path}: {e}. Using original.")
        return image_path


def process_images_to_base64(
    images: List[str], compress_for_retry: bool = False
) -> Tuple[List[str], List[str]]:
    """
    Process images (file paths or base64) and convert to base64 encoded strings.

    Args:
        images: List of image paths or base64 strings
        compress_for_retry: If True, compress images before processing (for 502 retry)

    Returns:
        Tuple of (list of base64 encoded image strings, list of temp files to cleanup)
    """
    processed_images = []
    temp_files_to_cleanup = []

    for img in images:
        if os.path.isfile(img):
            # It's a file path, validate and convert if needed
            image_path = img

            # Check file size and compress if needed (before retry compression)
            image_already_compressed = False

            # Compress image if this is a retry after 502 error
            if compress_for_retry:
                logger.info(f"Compressing image for retry: {img}")
                compressed_path = compress_image_for_retry(
                    image_path, compression_factor=0.7
                )
                if compressed_path != image_path:
                    image_path = compressed_path
                    temp_files_to_cleanup.append(compressed_path)
                    image_already_compressed = True  # Already compressed as JPEG

            # Validate and normalize image format using PIL if available
            # Skip PIL processing if image was already compressed (it's already in JPEG format)
            if PIL_AVAILABLE and not image_already_compressed:
                try:
                    with Image.open(image_path) as pil_image:
                        # Get image format
                        image_format = pil_image.format
                        logger.debug(f"Image format detected: {image_format} for {img}")

                        # Convert to RGB if necessary (for JPEG compatibility)
                        # Some formats like RGBA, P, LA need conversion
                        if pil_image.mode in ("RGBA", "LA", "P"):
                            # Create white background for transparent images
                            background = Image.new(
                                "RGB", pil_image.size, (255, 255, 255)
                            )
                            if pil_image.mode == "P":
                                pil_image = pil_image.convert("RGBA")
                            background.paste(
                                pil_image,
                                mask=(
                                    pil_image.split()[-1]
                                    if pil_image.mode == "RGBA"
                                    else None
                                ),
                            )
                            pil_image = background
                        elif pil_image.mode != "RGB":
                            pil_image = pil_image.convert("RGB")

                        # Save to temporary file in PNG format for consistency
                        # PNG is widely supported by vision models
                        temp_file = tempfile.NamedTemporaryFile(
                            delete=False, suffix=".png"
                        )
                        pil_image.save(temp_file.name, "PNG", optimize=True)
                        temp_file.close()
                        if image_path != img:
                            # Clean up previous temp file if exists
                            if image_path in temp_files_to_cleanup:
                                temp_files_to_cleanup.remove(image_path)
                        image_path = temp_file.name
                        temp_files_to_cleanup.append(temp_file.name)
                        logger.debug(f"Image normalized to PNG format: {image_path}")
                except Exception as e:
                    logger.warning(
                        f"Failed to validate/convert image {img} with PIL: {e}. "
                        f"Will try to use original file."
                    )
                    # Continue with original file if PIL processing fails

            # Read and encode to base64
            try:
                with open(image_path, "rb") as image_file:
                    image_data = image_file.read()
                    image_size_mb = len(image_data) / (1024 * 1024)
                    image_base64 = base64.b64encode(image_data).decode("utf-8")
                    base64_size_mb = len(image_base64.encode("utf-8")) / (1024 * 1024)
                    logger.debug(
                        f"Image file: {img}, "
                        f"original size: {image_size_mb:.2f} MB, "
                        f"base64 size: {base64_size_mb:.2f} MB"
                    )
                    if base64_size_mb > 10:
                        logger.warning(
                            f"Large image detected ({base64_size_mb:.2f} MB base64). "
                            f"This may cause issues with API."
                        )
                    processed_images.append(image_base64)

            except Exception as e:
                logger.error(f"Failed to read image file {img}: {e}")
                # Clean up temp files before raising
                for temp_file in temp_files_to_cleanup:
                    try:
                        if os.path.exists(temp_file):
                            os.unlink(temp_file)
                    except Exception:
                        pass
                raise ValueError(f"Failed to process image file {img}: {e}")
        else:
            # Assume it's already base64 encoded or a URL
            if img.startswith(("http://", "https://")):
                logger.warning(
                    f"Image appears to be a URL: {img}. "
                    f"Some APIs may not support URLs directly. Consider downloading the image first."
                )
            processed_images.append(img)

    return processed_images, temp_files_to_cleanup


def cleanup_temp_files(temp_files: List[str]) -> None:
    """
    Clean up temporary files.

    Args:
        temp_files: List of temporary file paths to delete
    """
    for temp_file in temp_files:
        try:
            if os.path.exists(temp_file):
                os.unlink(temp_file)
        except Exception as e:
            logger.debug(f"Failed to delete temporary file {temp_file}: {e}")
