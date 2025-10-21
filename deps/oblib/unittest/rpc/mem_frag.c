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

#include <linux/kernel.h>
#include <linux/skbuff.h>
#include <linux/rtnetlink.h>
#include <linux/netdevice.h>
#include <linux/ip.h>
#include <linux/tcp.h>

struct page_list {
  struct page_list *next;
};

static struct page_list *free_list;

uint64_t count=0;

#define PG_ORDER  2

int __init mem_frag_init(void)
{
  struct page *pg;
  struct page_list *tmp;
  struct page_list *prev, *next;

  free_list = NULL;
  while (1) {
    pg = alloc_pages(GFP_KERNEL, PG_ORDER);
    if (pg == NULL)
      break;
    tmp = (struct page_list *)page_address(pg);
    tmp->next = free_list;
    free_list = tmp;
    count++;
    //printk(KERN_DEBUG "PFN:%lu\n", page_to_pfn(pg));
  }

  printk(KERN_DEBUG "%s: alloc count:%llu\n", __func__, count);

  if (free_list == NULL)
    return -ENOMEM;
  //free half of pages back to buddy.
  prev = free_list;

  while (prev && prev->next) {
    next = prev->next;
    prev->next = next->next;
    free_pages((uint64_t)next, PG_ORDER);
    prev = prev->next;
    count--;
  }
  printk(KERN_DEBUG "%s: Fin count:%llu\n", __func__, count);

  return 0;
}

void __exit mem_frag_exit(void)
{
  struct page_list *tmp;

  while (free_list) {
    tmp = free_list;
    free_list = free_list->next;
    free_pages((uint64_t)tmp, PG_ORDER);
    count--;
  }
  printk(KERN_DEBUG "%s: exit. count:%llu\n", __func__, count);
}

module_init(mem_frag_init);
module_exit(mem_frag_exit);
MODULE_DESCRIPTION("make page fragment");
MODULE_LICENSE("GPL");
