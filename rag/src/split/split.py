import re
from typing import List


def split_text(
    text: str,
    max_chunk_size: int = 2000,
) -> List[str]:
    """
    Split text into chunks by paragraphs. Only split long paragraphs if they exceed max_chunk_size.

    Args:
        text: Input text to split
        max_chunk_size: Maximum size for a chunk (in characters).
                        Only paragraphs longer than this will be split further.

    Returns:
        List of text chunks, each chunk is typically a paragraph or part of a very long paragraph
    """
    if not text or not text.strip():
        return []

    # Normalize whitespace: replace multiple spaces/newlines with single space/newline
    text = re.sub(r"[ \t]+", " ", text)
    text = re.sub(r"\n{3,}", "\n\n", text)
    text = text.strip()

    # Split by paragraphs (double newlines)
    paragraphs = [p.strip() for p in text.split("\n\n") if p.strip()]

    if not paragraphs:
        return []

    chunks = []

    for paragraph in paragraphs:
        # If paragraph is too long, split it at sentence boundaries
        if len(paragraph) > max_chunk_size:
            # Split long paragraph by sentences
            long_chunks = _split_long_paragraph(paragraph, max_chunk_size)
            chunks.extend(long_chunks)
        else:
            # Keep paragraph as is
            chunks.append(paragraph)

    # Filter out empty chunks
    return [chunk for chunk in chunks if chunk.strip()]


def _split_long_paragraph(text: str, max_chunk_size: int) -> List[str]:
    """
    Split a long paragraph into smaller chunks at sentence boundaries.
    Only used when a single paragraph exceeds max_chunk_size.
    """
    # Find sentence boundaries (Chinese and English punctuation)
    sentence_endings = r"[。！？.!?]"

    # Split text while keeping the punctuation
    parts = re.split(f"({sentence_endings})", text)

    sentences = []
    current_sentence = ""

    for part in parts:
        if not part.strip():
            continue

        current_sentence += part

        # If this part is a sentence ending, save the sentence
        if re.match(sentence_endings, part):
            if current_sentence.strip():
                sentences.append(current_sentence.strip())
            current_sentence = ""

    # Add remaining text as a sentence if any
    if current_sentence.strip():
        sentences.append(current_sentence.strip())

    # If no sentence endings found, return the whole paragraph as one chunk
    if not sentences:
        return [text]

    # Group sentences into chunks
    chunks = []
    current_chunk = ""

    for sentence in sentences:
        # If adding this sentence would exceed max_chunk_size, save current chunk
        if current_chunk and len(current_chunk) + len(sentence) + 1 > max_chunk_size:
            chunks.append(current_chunk)
            current_chunk = sentence
        else:
            if current_chunk:
                current_chunk += " " + sentence
            else:
                current_chunk = sentence

    # Add remaining chunk
    if current_chunk:
        chunks.append(current_chunk)

    return chunks


if __name__ == "__main__":
    print(
        split_text(
            """
公司介绍：聚焦数字能源产业链，软、硬件协同发展：公司创立于 1996 年，2010 年于深交所挂牌上市，主要业务包含数据中心与站点能源、数字电网与综合能源服务、电力电源、新能源车充换电等。公司股权结构稳定，管理层资历深厚。2024 年公司利润实现高增，2025 年预计持续增长。2024 年，公司实现营业收入 19.6 亿元，同比+26.1%；实现归母净利润 1.1 亿元，同比 +178.5%。数据中心电源、电力操作电源系统、通信电源系统贡献主要营收，2024 年，三者在营收中占比分别为 34.1%、24.4%、15.6%。
        
硬件业务：电源业务基本盘稳定，HVDC 迎来市场新机遇：

1）HVDC 方案先行者， AIDC 时代渗透率有望提升： 当前数据中心常见的几种不间断供电技术包括 AC UPS、HVDC 和市电直供+BBU 等。AIGC 时代，HVDC 供电凭借输电距离长、损耗小、稳定性好、效率高等优点，渗透率有望提升。公司是 HVDC 方案先行者，市场份额行业领先，牵头制订了《信息通信用 240V/336V 直流供电系统技术要求和试验方法》国家标准，且入选工信部制造业单项冠军示范企业。目前，公司主要客户为大型互联网企业、运营商以及 IDC 第三方数据中心运营商。

2）巴拿马方案绑定大客户，有望持续贡献业绩：预制化 Panama 电力模组是公司与大客户阿里巴巴合作开发的数据中心供电方案，有占地面积小、交付速度快、高可用性、效率高、成本低等优点。阿里巴巴未来三年在云和 AI的基础设施投入预计将超越过去十年的总和，有望带动公司巴拿马电源产品出货量增长；此外，头部互联网企业及运营商均规划增加算力相关资本开支，若能获取其他头部企业大订单，公司业绩增长空间有望进一步向上打开。

3）通信电源维持稳定，电力电源拓展存量份额：通信电源来看，公司在近年运营商集采招标中份额位居行业前列，营业收入基本稳定。2024 年，公司通信电源营收 3.06 亿元，同比-4.5%。电力电源来看，公司通过深耕细作继续拓展存量市场份额；另一方面，加速推进海外业务，并拓展新能源等增量市场机会，营收维持稳定增长。 2024 年，公司电力电源营收 4.79 亿元，同比+20.5%。
        """
        )
    )
