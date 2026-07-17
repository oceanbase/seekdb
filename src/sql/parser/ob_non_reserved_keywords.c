/*
 * Copyright (c) 2025 OceanBase.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "ob_non_reserved_keywords.h"
#include <stdio.h>
#include "lib/utility/alloc_assist.h"
#include <stdlib.h>
#include "sql/parser/parse_define.h"

int32_t get_next_id(char c)
{
  int32_t ch_id = -1;
  if (c >= 'A' && c <= 'Z') {
    c += 'a' - 'A';
  }

  if ('_' == c) {
    ch_id = 36;
  } else if (c >= 'a' && c <= 'z') {
    ch_id = c - 'a';
  } else if (c >= '0' && c <= '9') {
    ch_id = c - '0' + 26;
  }
  return ch_id;
}


//return 0 if succ, return 1 if fail
int add_word(t_node *root, const char *str, const int32_t idx)
{
  int ret = 0;
  t_node *pt = root;
  if (OB_UNLIKELY(NULL == root)) {
    ret = 1;
    printf("ERROR root is NULL! \n");
  } else if (OB_UNLIKELY(NULL == str)) {
    ret = 1;
    printf("ERROR word str is NULL! \n");
  } else if (OB_UNLIKELY(idx < 0)) {
    printf("ERROR invalid idx:%d\n", idx);
  } else {
    for ( ; '\0' != *str && 0 == ret; ++str) {
      int32_t ch_id = get_next_id(*str);
      if (ch_id >= 0 && NULL == pt->next[ch_id]) {
        t_node *new_node = (t_node *)calloc(1, sizeof(t_node));
        if (OB_UNLIKELY(NULL == new_node)) {
          ret = OB_PARSER_ERR_NO_MEMORY;
          printf("ERROR malloc memory failed! \n");
        } else {
          new_node->idx = -1;
          pt->next[ch_id] = new_node;
        }
      }
      if (OB_LIKELY(0 == ret)) {
        if (OB_LIKELY(ch_id >= 0)) {
          pt = pt->next[ch_id];
        } else {
          printf("ERROR ob_non_reserved_keywords.c: wrong index! \n");
          ret = 1;
        }
      }
    }
  }
  if (0 == ret) {
    pt->idx = idx;
  }
  return ret;
}

const NonReservedKeyword *find_word(const char *word, const t_node *root, const NonReservedKeyword *words)
{
  const NonReservedKeyword *res_word = NULL;
  const t_node *pt = root;
  if (OB_UNLIKELY(NULL == word)) {
    //do nothing
  } else {
    for (; *word != '\0' && NULL != pt; ++word) {
      char c = *word;
      int32_t ch_id = get_next_id(c);
      if (ch_id < 0) {
        pt = NULL;
      } else {
        pt = pt->next[ch_id];
      }
    }
  }
  if (OB_LIKELY(NULL != pt && -1 != pt->idx)) {
    res_word = &words[pt->idx];
  }
  return res_word;
}

static int is_same_prefix(const char *lhs, const char *rhs, const int32_t prefix_len)
{
  int ret = 1;
  int32_t i = 0;
  for (; 1 == ret && i < prefix_len; ++i) {
    if ('\0' == lhs[i] || '\0' == rhs[i]) {
      ret = 0;
    } else {
      int32_t lhs_id = get_next_id(lhs[i]);
      int32_t rhs_id = get_next_id(rhs[i]);
      if (lhs_id < 0 || rhs_id < 0 || lhs_id != rhs_id) {
        ret = 0;
      }
    }
  }
  return ret;
}

static int has_previous_prefix(const NonReservedKeyword *words, const int32_t word_idx,
                               const char *word, const int32_t prefix_len)
{
  int exist = 0;
  int32_t i = 0;
  for (; 0 == exist && i < word_idx; ++i) {
    exist = is_same_prefix(words[i].keyword_name, word, prefix_len);
  }
  return exist;
}

static int count_trie_tree_nodes(const NonReservedKeyword *words, const int32_t count, int32_t *node_count)
{
  int ret = 0;
  if (OB_UNLIKELY(NULL == words || NULL == node_count || count < 0)) {
    ret = 1;
  } else {
    *node_count = 1; // root
    int32_t i = 0;
    for (; 0 == ret && i < count; ++i) {
      const char *word = words[i].keyword_name;
      if (OB_UNLIKELY(NULL == word)) {
        ret = 1;
        (void)printf("ERROR word str is NULL! \n");
      } else {
        int32_t prefix_len = 0;
        for (; 0 == ret && '\0' != word[prefix_len]; ++prefix_len) {
          if (OB_UNLIKELY(get_next_id(word[prefix_len]) < 0)) {
            ret = 1;
            (void)printf("ERROR ob_non_reserved_keywords.c: wrong index! \n");
          } else if (!has_previous_prefix(words, i, word, prefix_len + 1)) {
            ++(*node_count);
          }
        }
      }
    }
  }
  return ret;
}

static int add_word_from_pool(t_node *root, const char *str, const int32_t idx,
                              t_node *nodes, const int32_t node_count, int32_t *next_node_idx)
{
  int ret = 0;
  t_node *pt = root;
  if (OB_UNLIKELY(NULL == root || NULL == nodes || NULL == next_node_idx)) {
    ret = 1;
    printf("ERROR root is NULL! \n");
  } else if (OB_UNLIKELY(NULL == str)) {
    ret = 1;
    printf("ERROR word str is NULL! \n");
  } else if (OB_UNLIKELY(idx < 0)) {
    ret = 1;
    printf("ERROR invalid idx:%d\n", idx);
  } else {
    for ( ; '\0' != *str && 0 == ret; ++str) {
      int32_t ch_id = get_next_id(*str);
      if (ch_id >= 0 && NULL == pt->next[ch_id]) {
        if (OB_UNLIKELY(*next_node_idx >= node_count)) {
          ret = OB_PARSER_ERR_NO_MEMORY;
          printf("ERROR trie node pool exhausted! \n");
        } else {
          t_node *new_node = &nodes[(*next_node_idx)++];
          new_node->idx = -1;
          pt->next[ch_id] = new_node;
        }
      }
      if (OB_LIKELY(0 == ret)) {
        if (OB_LIKELY(ch_id >= 0)) {
          pt = pt->next[ch_id];
        } else {
          printf("ERROR ob_non_reserved_keywords.c: wrong index! \n");
          ret = 1;
        }
      }
    }
  }
  if (0 == ret) {
    pt->idx = idx;
  }
  return ret;
}

//return 0 if succ, return 1 if fail
int create_trie_tree(const NonReservedKeyword *words, int32_t count, t_node **root)
{
  int ret = 0;
  int32_t node_count = 0;
  if (OB_UNLIKELY(NULL == root)) {
    (void)printf("ERROR invalid root! \n");
    ret = 1;
  } else if (0 != (ret = count_trie_tree_nodes(words, count, &node_count))) {
    (void)printf("ERROR count trie tree nodes failed! \n");
  } else {
    t_node *nodes = (t_node *)calloc(node_count, sizeof(t_node));
    if (OB_UNLIKELY(NULL == nodes)) {
      (void)printf("ERROR malloc memory failed! \n");
      ret = 1;
    } else {
      nodes[0].idx = -1;
      int32_t next_node_idx = 1;
      int32_t i = 0;
      for (; 0 == ret && i < count; ++i) {
        ret = add_word_from_pool(nodes, words[i].keyword_name, i, nodes, node_count, &next_node_idx);
      }
      if (0 == ret) {
        *root = nodes;
      } else {
        free(nodes);
        *root = NULL;
      }
    }
  }
  return ret;
}
