import argparse
import logging
import re
from typing import List, Optional

import dotenv

from src.integrations.llm import generate_response
from src.util import Answer, read_train_json

dotenv.load_dotenv()

_logger = logging.getLogger("eval")


def llm_judge(origin_text: str, text: str) -> Optional[float]:
    """
    使用大模型评估两个文本的相关性/相似性

    Args:
        origin_text: 原始文本（参考文本或查询）
        text: 待评估的文本

    Returns:
        相似性评分，范围 0.0-1.0
    """
    if origin_text == text:
        return 1.0

    prompt = f"""你是一个专业的文本相关性/相似性评估专家。请评估以下两个文本之间的语义相关性和相似度。

原始文本（参考）: {origin_text}

待评估文本: {text}

评分标准（0.0-1.0分，与检索系统评分范围一致）：
- 0.95-1.0: 极高相关性/相似性，语义高度一致，主题、内容、意图几乎完全相同
- 0.85-0.94: 高度相关/相似，核心语义一致，主题高度匹配，存在细微差异
- 0.75-0.84: 高度相关/相似，主要语义一致，主题匹配，但存在一定差异
- 0.65-0.74: 中等相关/相似，语义部分一致，主题相关，但侧重点或细节有所不同
- 0.55-0.64: 中等相关/相似，语义有一定重叠，主题有一定关联，但差异明显
- 0.45-0.54: 低度相关/相似，语义关联性较弱，主题略有相关，但差异较大
- 0.35-0.44: 低度相关/相似，语义关联性很弱，主题关联性有限
- 0.25-0.34: 几乎不相关/相似，语义几乎无关联，主题基本不同
- 0.15-0.24: 不相关/不相似，语义无关联，主题完全不同
- 0.05-0.14: 完全不相关/不相似，语义完全无关，主题完全无关
- 0.0-0.04: 无关或空内容

评分原则：
1. 语义相似性：评估两个文本在语义层面的相似程度，包括核心概念、主题、意图的一致性
2. 内容重叠度：评估两个文本在具体内容、信息、细节上的重叠程度
3. 主题相关性：评估两个文本在主题、领域、话题上的相关程度
4. 上下文关联性：评估两个文本在上下文语境中的关联强度
5. 使用连续评分，基于实际相似程度给出精确分数，避免极端评分（0.0或1.0），除非确实完全相关或完全无关

请只返回一个0.0到1.0之间的浮点数评分，不要包含任何其他文字说明。"""

    try:
        # 调用大模型API
        response = generate_response(
            [
                {"role": "user", "content": prompt},
            ],
            model="qwen-max",
        )

        # 从响应中提取评分
        similarity = extract_score(response)

    except Exception as e:
        logging.error(f"Error in llm_judge: {e}")
        similarity = None

    return similarity


def extract_score(response_text: str) -> float:
    """
    从大模型响应中提取评分

    Args:
        response_text: 大模型返回的文本

    Returns:
        评分值（0.0-1.0）
    """
    # 尝试直接提取浮点数
    # 匹配0.0到1.0之间的浮点数
    pattern = r"\b(0\.\d+|1\.0|1)\b"
    matches = re.findall(pattern, response_text)

    if matches:
        try:
            score = float(matches[0])
            # 确保分数在0.0-1.0范围内
            if score < 0.0:
                return 0.0
            elif score > 1.0:
                return 1.0
            return score
        except ValueError:
            pass

    # 如果找不到数字，尝试其他格式
    # 例如：评分：0.85 或 score: 0.85
    pattern = r"[0-1]\.?\d*"
    matches = re.findall(pattern, response_text)

    if matches:
        try:
            score = float(matches[0])
            if score < 0.0:
                return 0.0
            elif score > 1.0:
                return 1.0
            return score
        except ValueError:
            pass

    # 如果都提取不到，返回默认值0.0
    logging.warning(f"Warning: Could not extract score from response: {response_text}")
    return 0.0


def evaluation_all(player_answers: List[Answer], normal_answers: List[Answer]) -> float:
    _logger.info(
        "Starting answer evaluation, player_answers count: %d, normal_answers count: %d",
        len(player_answers),
        len(normal_answers),
    )

    if len(player_answers) != len(normal_answers):
        _logger.error(
            "Answer count mismatch: player_answers=%d, normal_answers=%d",
            len(player_answers),
            len(normal_answers),
        )
        raise ValueError("answers must have same length")

    score = 0.0
    count = 0
    total_items = len(player_answers)
    failed_items = 0

    _logger.info("Starting item-by-item evaluation, total items: %d", total_items)

    for idx, (player_answer, normal_answer) in enumerate(
        zip(player_answers, normal_answers), 1
    ):
        _logger.info(
            "Evaluating item %d/%d: question='%s'",
            idx,
            total_items,
            player_answer.question[:50] if player_answer.question else "",
        )

        s = evaluation(player_answer, normal_answer)
        if s is None:
            failed_items += 1
            _logger.info("Item %d/%d evaluation failed, skipping", idx, total_items)
            continue

        score += s
        count += 1
        _logger.info(
            "Item %d/%d score: %.4f (cumulative score: %.4f, valid items: %d)",
            idx,
            total_items,
            s,
            score,
            count,
        )

    if count == 0:
        _logger.error("All evaluation items failed, cannot calculate average score")
        raise ValueError("All evaluations failed, cannot calculate average score")

    final_score = score / count
    _logger.info(
        "Evaluation completed: total_items=%d, successful_items=%d, failed_items=%d, total_score=%.4f, average_score=%.4f",
        total_items,
        count,
        failed_items,
        score,
        final_score,
    )

    return final_score


def evaluation(player_answer: Answer, normal_answer: Answer) -> Optional[float]:
    _logger.debug(
        "Evaluating single answer: question='%s'",
        player_answer.question[:50] if player_answer.question else "",
    )

    answer_score = llm_judge(
        normal_answer.answer,
        player_answer.answer,
    )

    if answer_score is None:
        _logger.warning(
            "LLM evaluation failed: question='%s'",
            player_answer.question[:50] if player_answer.question else "",
        )
        return None

    filename_score = 0.0
    page_score = 0.0

    if player_answer.filename == normal_answer.filename:
        filename_score = 1.0
        _logger.debug("Filename matched: '%s'", player_answer.filename)

        if player_answer.page == normal_answer.page:
            page_score = 1.0
            _logger.debug("Page number matched: %d", player_answer.page)
        else:
            _logger.debug(
                "Page number mismatch: player=%d, normal=%d",
                player_answer.page,
                normal_answer.page,
            )
    else:
        _logger.debug(
            "Filename mismatch: player='%s', normal='%s'",
            player_answer.filename,
            normal_answer.filename,
        )

    final_score = filename_score * 25 + page_score * 25 + answer_score * 50
    _logger.debug(
        "Item score calculation: answer_score=%.4f, filename_score=%.4f, page_score=%.4f, final_score=%.4f",
        answer_score,
        filename_score,
        page_score,
        final_score,
    )

    return final_score


def run(
    output_file: str,
    answers_file: str,
) -> float:
    outputs = read_train_json(output_file)
    answers = read_train_json(answers_file)

    return evaluation_all(outputs, answers)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="Evaluate answers against ground truth",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  python3 eval.py --output ./data/output.json --answer ./data/train.json
        """,
    )
    parser.add_argument(
        "--output",
        type=str,
        default="./data/output.json",
        help="Path to the output JSON file (player answers)",
    )
    parser.add_argument(
        "--answer",
        type=str,
        default="./data/answer.json",
        help="Path to the answer JSON file (ground truth)",
    )

    args = parser.parse_args()

    logging.basicConfig(
        level=logging.DEBUG,
        format="%(asctime)s - %(name)s - %(levelname)s - %(message)s",
    )

    try:
        final_score = run(args.output, args.answer)
        _logger.info(f"\nFinal evaluation score: {final_score:.4f}")
    except Exception as e:
        _logger.error(f"Evaluation failed: {e}", exc_info=True)
        exit(1)
