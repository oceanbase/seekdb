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

class Printer
{
public:
  enum { MAX_BUF_SIZE = 4096};
  Printer(): limit_(MAX_BUF_SIZE), pos_(0)
  {
    memset(buf_, 0, MAX_BUF_SIZE);
  }

  ~Printer()
  {
    pos_ = 0;
  }
  void reset()
  {
    pos_ = 0;
    *buf_ = 0;
  }
  char *get_str() { return NULL != buf_ && limit_ > 0 ? buf_ : NULL; }
  char *append(const char *format, ...)
  {
    char *src = NULL;
    int64_t count = 0;
    va_list ap;
    va_start(ap, format);
    if (NULL != buf_ && limit_ > 0 && pos_ < limit_
        && pos_ + (count = vsnprintf(buf_ + pos_, limit_ - pos_, format, ap)) < limit_) {
      src = buf_ + pos_;
      pos_ += count;
    }
    va_end(ap);
    return src;
  }
  char *new_str(const char *format, ...)
  {
    char *src = NULL;
    int64_t count = 0;
    va_list ap;
    va_start(ap, format);
    if (NULL != buf_ && limit_ > 0 && pos_ < limit_
        && pos_ + (count = vsnprintf(buf_ + pos_, limit_ - pos_, format, ap)) + 1 < limit_) {
      src = buf_ + pos_;
      pos_ += count + 1;
    }
    va_end(ap);
    return src;
  }
private:
  char buf_[MAX_BUF_SIZE];
  int64_t limit_;
  int64_t pos_;
};

class Tokenizer
{
public:
  Tokenizer(char* str, const char* delim): str_(str), delim_(delim), saveptr_(NULL)
  {}
  ~Tokenizer() {}
  const char* next() {
    char* ret = NULL;
    if (NULL == saveptr_) {
      ret = strtok_r(str_, delim_, &saveptr_);
    } else {
      ret = strtok_r(NULL, delim_, &saveptr_);
    }
    return ret;
  }
private:
  char* str_;
  const char* delim_;
  char* saveptr_;
};

