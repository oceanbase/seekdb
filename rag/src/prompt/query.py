"""Prompts for query/answering functionality."""

# System prompt for the document Q&A assistant
QUERY_SYSTEM_PROMPT = "你是一个专业的文档问答助手，能够基于提供的文档内容准确回答问题。"

# User prompt template for answering questions based on document content
QUERY_USER_PROMPT_TEMPLATE = """基于以下文档内容回答问题。如果文档中没有相关信息，请说明无法从提供的文档中找到答案。

文档内容：
{context_text}

问题：{question}

请基于上述文档内容回答问题，确保答案准确、完整。如果文档中没有相关信息，请明确说明。"""
