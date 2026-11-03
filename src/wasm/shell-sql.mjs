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

function scanStatements(sql, { noBackslashEscapes = false, ansiQuotes = false } = {}, firstOnly = false) {
  if (typeof sql !== 'string') throw new TypeError('SQL must be a string.');
  const statements = [];
  let start = 0;
  let hasContent = false;
  let index = 0;

  while (index < sql.length) {
    const character = sql[index];
    if (/\s/u.test(character)) {
      index++;
    } else if (character === ';') {
      if (hasContent) {
        statements.push(sql.slice(start, index).trim());
        if (firstOnly) return { statements, complete: true, rest: sql.slice(index + 1) };
      }
      hasContent = false;
      start = ++index;
    } else if (character === '#' || (sql.startsWith('--', index)
      && (index + 2 === sql.length || /[\s\u0000-\u001f\u007f]/u.test(sql[index + 2])))) {
      while (index < sql.length && sql[index] !== '\n' && sql[index] !== '\r') index++;
    } else if (sql.startsWith('/*', index)) {
      const end = sql.indexOf('*/', index + 2);
      if (end < 0) throw new SyntaxError(`Unterminated block comment at character ${index + 1}.`);
      if (sql[index + 2] === '!'
        && sql.slice(index + 3, end).replace(/^\d{5,6}(?=\s|$)/u, '').trim()) {
        hasContent = true;
      }
      index = end + 2;
    } else if (character === "'" || character === '"' || character === '`') {
      hasContent = true;
      const quoteStart = index++;
      const identifier = character === '`' || (character === '"' && ansiQuotes);
      let closed = false;
      while (index < sql.length) {
        if (sql[index] === '\\' && !identifier && !noBackslashEscapes) {
          index += 2;
        } else if (sql[index] === character) {
          if (sql[index + 1] === character) {
            index += 2;
          } else {
            index++;
            closed = true;
            break;
          }
        } else {
          index++;
        }
      }
      if (!closed) {
        const kind = identifier ? 'quoted identifier' : 'quoted string';
        throw new SyntaxError(`Unterminated ${kind} at character ${quoteStart + 1}.`);
      }
    } else {
      hasContent = true;
      index++;
    }
  }

  const complete = statements.length > 0 && !hasContent;
  if (hasContent) statements.push(sql.slice(start).trim());
  return { statements, complete };
}

export function splitStatements(sql, options) {
  return scanStatements(sql, options).statements;
}

export function takeStatement(sql, options) {
  const result = scanStatements(sql, options, true);
  if (!result.statements.length) return undefined;
  return { statement: result.statements[0], rest: result.rest ?? '' };
}

export function isComplete(sql, options) {
  try {
    return scanStatements(sql, options).complete;
  } catch (error) {
    if (error instanceof SyntaxError) return false;
    throw error;
  }
}

function scanInteractiveInput(sql, { noBackslashEscapes = false, ansiQuotes = false, delimiter = ';' } = {}, firstOnly = false) {
  if (typeof sql !== 'string') throw new TypeError('SQL must be a string.');
  if (typeof delimiter !== 'string' || !delimiter.length) throw new TypeError('Delimiter must be a non-empty string.');
  let index = 0;
  let start = 0;
  let hasContent = false;

  while (index < sql.length) {
    const character = sql[index];
    const shortCommand = character === '\\' && ['g', 'G', 'c', 'p'].includes(sql[index + 1]);
    if (shortCommand || sql.startsWith(delimiter, index)) {
      const terminator = shortCommand ? sql.slice(index, index + 2) : delimiter;
      const command = shortCommand && sql[index + 1] === 'c' ? 'clear'
        : shortCommand && sql[index + 1] === 'p' ? 'print' : undefined;
      if (firstOnly) {
        const statement = command ? sql.slice(start, index) : hasContent ? sql.slice(start, index).trim() : '';
        const rest = sql.slice(index + terminator.length);
        return { next: command ? { command, statement, rest, terminator }
          : { statement, rest, terminator, vertical: shortCommand && sql[index + 1] === 'G' } };
      }
      index += terminator.length;
      if (command !== 'print') {
        start = index;
        hasContent = false;
      }
    } else if (/\s/u.test(character)) {
      index++;
    } else if (character === '#' || (sql.startsWith('--', index)
      && (index + 2 === sql.length || /[\s\u0000-\u001f\u007f]/u.test(sql[index + 2])))) {
      while (index < sql.length && sql[index] !== '\n' && sql[index] !== '\r') index++;
    } else if (sql.startsWith('/*', index)) {
      const end = sql.indexOf('*/', index + 2);
      if (end < 0) return { prompt: '/*>' };
      if (sql[index + 2] === '!'
        && sql.slice(index + 3, end).replace(/^\d{5,6}(?=\s|$)/u, '').trim()) {
        hasContent = true;
      }
      index = end + 2;
    } else if (character === "'" || character === '"' || character === '`') {
      hasContent = true;
      const identifier = character === '`' || (character === '"' && ansiQuotes);
      let closed = false;
      index++;
      while (index < sql.length) {
        if (sql[index] === '\\' && !identifier && !noBackslashEscapes) {
          index += 2;
        } else if (sql[index] === character) {
          if (sql[index + 1] === character) {
            index += 2;
          } else {
            index++;
            closed = true;
            break;
          }
        } else {
          index++;
        }
      }
      if (!closed) return { prompt: `${character}>` };
    } else {
      hasContent = true;
      index++;
    }
  }
  return { prompt: hasContent ? '->' : '' };
}

export function takeInteractiveStatement(sql, options) {
  return scanInteractiveInput(sql, options, true).next;
}

export function interactivePrompt(sql, options) {
  return scanInteractiveInput(sql, options).prompt;
}

function* statementTokens(sql, { noBackslashEscapes = false, ansiQuotes = false } = {}) {
  let index = 0;
  const lineComment = offset => sql[offset] === '#' || (sql.startsWith('--', offset)
    && (offset + 2 === sql.length || /[\s\u0000-\u001f\u007f]/u.test(sql[offset + 2])));
  while (index < sql.length) {
    const character = sql[index];
    if (/\s/u.test(character) || character === ';') {
      index++;
    } else if (lineComment(index)) {
      while (index < sql.length && sql[index] !== '\n' && sql[index] !== '\r') index++;
    } else if (sql.startsWith('/*', index)) {
      const end = sql.indexOf('*/', index + 2);
      if (end < 0) throw new SyntaxError('Unterminated SQL comment.');
      if (sql[index + 2] === '!') {
        const content = sql.slice(index + 3, end).replace(/^\d{5}/u, '');
        yield* statementTokens(content, { noBackslashEscapes, ansiQuotes });
      }
      index = end + 2;
    } else if (character === '`' || character === '"' || character === "'") {
      const identifier = character === '`' || (character === '"' && ansiQuotes);
      let value = '';
      let closed = false;
      index++;
      while (index < sql.length) {
        if (sql[index] === '\\' && !identifier && !noBackslashEscapes) {
          const escapes = { '0': '\0', b: '\b', n: '\n', r: '\r', t: '\t', Z: '\x1a' };
          const next = sql[++index];
          value += escapes[next] ?? next ?? '';
          index++;
        } else if (sql[index] === character) {
          index++;
          if (sql[index] === character) {
            value += character;
            index++;
          } else {
            closed = true;
            break;
          }
        } else {
          value += sql[index++];
        }
      }
      if (!closed) throw new SyntaxError('Unterminated SQL quote.');
      yield value;
    } else {
      const start = index++;
      while (index < sql.length && !/[\s;'"`]/u.test(sql[index])
        && !lineComment(index) && !sql.startsWith('/*', index)) index++;
      yield sql.slice(start, index);
    }
  }
}

export function databaseAfterStatement(sql, currentDatabase, options) {
  try {
    const iterator = statementTokens(sql, options);
    const first = iterator.next().value?.toUpperCase();
    if (first !== 'USE' && first !== 'DROP') return currentDatabase;
    const second = iterator.next().value;
    if (first === 'USE') return second !== undefined && iterator.next().done ? second : currentDatabase;
    if (['DATABASE', 'SCHEMA'].includes(second?.toUpperCase())) {
      const tokens = [...iterator];
      const name = tokens.length === 1 ? tokens[0]
        : tokens.length === 3 && tokens[0].toUpperCase() === 'IF' && tokens[1].toUpperCase() === 'EXISTS' ? tokens[2] : undefined;
      if (name === currentDatabase) return null;
    }
  } catch (error) {
    if (!(error instanceof SyntaxError)) throw error;
  }
  return currentDatabase;
}

export class CommandHistory {
  #entries = [];
  #position = 0;
  #draft = '';
  #limit;

  constructor(limit = 100) {
    if (!Number.isInteger(limit) || limit < 1) {
      throw new RangeError('History limit must be a positive integer.');
    }
    this.#limit = limit;
  }

  push(sql) {
    const entry = sql.trim();
    if (entry && entry !== this.#entries.at(-1)) {
      this.#entries.push(entry);
      if (this.#entries.length > this.#limit) this.#entries.shift();
    }
    this.resetNavigation();
  }

  resetNavigation() {
    this.#position = this.#entries.length;
    this.#draft = '';
  }

  previous(draft = '') {
    if (!this.#entries.length) return undefined;
    if (this.#position === this.#entries.length) this.#draft = draft;
    this.#position = Math.max(0, this.#position - 1);
    return this.#entries[this.#position];
  }

  next() {
    if (this.#position === this.#entries.length) return undefined;
    this.#position++;
    return this.#position === this.#entries.length ? this.#draft : this.#entries[this.#position];
  }
}
