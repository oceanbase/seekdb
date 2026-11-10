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

const segmenter = new Intl.Segmenter('en', { granularity: 'grapheme' });
const escapes = new Map([
  ['\\', '\\\\'], ['\0', '\\0'], ['\b', '\\b'], ['\f', '\\f'],
  ['\n', '\\n'], ['\r', '\\r'], ['\t', '\\t'], ['\v', '\\v'],
]);

function visibleText(value) {
  if (value === null) return 'NULL';
  if (typeof value !== 'string') throw new TypeError('Table values must be strings or null.');
  if (!value.length) return "''";
  return value.replace(/[\\\p{Cc}\u00ad\u061c\u200b\u200e\u200f\u2028-\u202e\u2060-\u206f\ufeff]/gu, character => {
    if (escapes.has(character)) return escapes.get(character);
    const code = character.codePointAt(0);
    return code <= 0xff ? `\\x${code.toString(16).padStart(2, '0')}`
      : `\\u${code.toString(16).padStart(4, '0')}`;
  });
}

function isWide(code) {
  return (code >= 0x1100 && code <= 0x115f)
    || (code >= 0x2329 && code <= 0x232a)
    || (code >= 0x2e80 && code <= 0xa4cf && code !== 0x303f)
    || (code >= 0xac00 && code <= 0xd7a3)
    || (code >= 0xf900 && code <= 0xfaff)
    || (code >= 0xfe10 && code <= 0xfe19)
    || (code >= 0xfe30 && code <= 0xfe6f)
    || (code >= 0xff01 && code <= 0xff60)
    || (code >= 0xffe0 && code <= 0xffe6)
    || (code >= 0x16fe0 && code <= 0x16fe4)
    || (code >= 0x17000 && code <= 0x18dff)
    || (code >= 0x1aff0 && code <= 0x1afff)
    || (code >= 0x1b000 && code <= 0x1b2ff)
    || (code >= 0x1f200 && code <= 0x1f251)
    || (code >= 0x1f300 && code <= 0x1faff)
    || (code >= 0x20000 && code <= 0x3fffd);
}

function displayWidth(text) {
  if (/^[\x20-\x7e]*$/u.test(text)) return text.length;
  let width = 0;
  for (const { segment } of segmenter.segment(text)) {
    if (/\p{Emoji_Presentation}/u.test(segment)
      || (/\ufe0f/u.test(segment) && /\p{Emoji}/u.test(segment))
      || /[0-9#*]\ufe0f?\u20e3/u.test(segment)) {
      width += 2;
      continue;
    }
    for (const character of segment.normalize('NFC')) {
      if (/[\p{Mark}\p{Cf}]/u.test(character)) continue;
      width += isWide(character.codePointAt(0)) ? 2 : 1;
    }
  }
  return width;
}

export class TableFormatter {
  #table;
  #widths;
  #extraCharacters;
  #maxCharacters;
  #limited = false;
  #formatted;

  constructor(columns, {maxCharacters = Infinity} = {}) {
    if (!Array.isArray(columns) || columns.some(column => typeof column !== 'string')) {
      throw new TypeError('Table columns must be an array of strings.');
    }
    if (maxCharacters !== Infinity && (!Number.isSafeInteger(maxCharacters) || maxCharacters < 0)) {
      throw new RangeError('The table character limit must be a nonnegative integer.');
    }
    const header = this.#prepare(columns);
    this.#table = [header];
    this.#widths = header.map(cell => cell.width);
    this.#extraCharacters = header.reduce((sum, cell) => sum + cell.text.length - cell.width, 0);
    this.#maxCharacters = maxCharacters;
    this.#limited = this.#length(this.#widths, 0, this.#extraCharacters) > maxCharacters;
  }

  #prepare(row) {
    return row.map(value => {
      const text = visibleText(value);
      return {text, width: displayWidth(text)};
    });
  }

  #length(widths, rows, extraCharacters) {
    if (!widths.length) return 0;
    return (rows + 4) * (widths.reduce((sum, width) => sum + width, 0) + 3 * widths.length + 1)
      + rows + 3 + extraCharacters;
  }

  get rowCount() { return this.#table.length - 1; }
  get limited() { return this.#limited; }

  append(row) {
    if (!Array.isArray(row) || row.length !== this.#widths.length) {
      throw new TypeError('Each table row must match the column count.');
    }
    if (this.#limited) return false;
    const cells = this.#prepare(row);
    const widths = this.#widths.map((width, index) => Math.max(width, cells[index].width));
    const extraCharacters = this.#extraCharacters + cells.reduce((sum, cell) => sum + cell.text.length - cell.width, 0);
    if (this.#length(widths, this.rowCount + 1, extraCharacters) > this.#maxCharacters) {
      this.#limited = true;
      return false;
    }
    this.#table.push(cells);
    this.#widths = widths;
    this.#extraCharacters = extraCharacters;
    this.#formatted = undefined;
    return true;
  }

  format() {
    if (!this.#widths.length || this.#length(this.#widths, this.rowCount, this.#extraCharacters) > this.#maxCharacters) return '';
    if (this.#formatted !== undefined) return this.#formatted;
    const border = `+${this.#widths.map(width => '-'.repeat(width + 2)).join('+')}+`;
    const line = row => `| ${row.map((cell, index) => cell.text + ' '.repeat(this.#widths[index] - cell.width)).join(' | ')} |`;
    this.#formatted = [border, line(this.#table[0]), border, ...this.#table.slice(1).map(line), border].join('\n');
    return this.#formatted;
  }
}

export class VerticalFormatter {
  #columnCount;
  #labels = [];
  #labelCharacters = 0;
  #rows = [];
  #characters = 0;
  #maxCharacters;
  #limited = false;
  #formatted = '';

  constructor(columns, {maxCharacters = Infinity} = {}) {
    if (!Array.isArray(columns) || columns.some(column => typeof column !== 'string')) {
      throw new TypeError('Table columns must be an array of strings.');
    }
    if (maxCharacters !== Infinity && (!Number.isSafeInteger(maxCharacters) || maxCharacters < 0)) {
      throw new RangeError('The table character limit must be a nonnegative integer.');
    }
    this.#columnCount = columns.length;
    this.#maxCharacters = maxCharacters;
    let characters = 0;
    let width = 0;
    const labels = [];
    for (const column of columns) {
      const text = this.#prepare(column, maxCharacters - characters);
      if (text === undefined) { this.#limited = true; return; }
      characters += text.length;
      const labelWidth = displayWidth(text);
      width = Math.max(width, labelWidth);
      labels.push({text, width: labelWidth});
    }
    this.#labelCharacters = labels.reduce((sum, label) => sum + width - label.width + label.text.length + 3, 0);
    if (this.#header().length + this.#labelCharacters > maxCharacters) {
      this.#limited = true;
      return;
    }
    this.#labels = labels.map(label => ' '.repeat(width - label.width) + label.text + ': ');
  }

  #prepare(value, maxCharacters) {
    if (value === null || typeof value !== 'string' || !value.length || maxCharacters === Infinity) {
      const text = visibleText(value);
      return text.length <= maxCharacters ? text : undefined;
    }
    if (value.length > maxCharacters) return;
    const parts = [];
    let characters = 0;
    for (let offset = 0; offset < value.length; offset += 256) {
      const part = visibleText(value.slice(offset, offset + 256));
      if (part.length > maxCharacters - characters) return;
      parts.push(part);
      characters += part.length;
    }
    return parts.join('');
  }

  #header() {
    return `*************************** ${this.rowCount + 1}. row ***************************`;
  }

  get rowCount() { return this.#rows.length; }
  get limited() { return this.#limited; }

  append(row) {
    if (!Array.isArray(row) || row.length !== this.#columnCount) {
      throw new TypeError('Each table row must match the column count.');
    }
    if (this.#limited) return false;
    const header = this.#header();
    const separator = this.rowCount ? 1 : 0;
    let remaining = this.#maxCharacters - this.#characters - separator - header.length - this.#labelCharacters;
    if (remaining < 0) { this.#limited = true; return false; }
    const values = [];
    for (const value of row) {
      const text = this.#prepare(value, remaining);
      if (text === undefined) { this.#limited = true; return false; }
      values.push(text);
      remaining -= text.length;
    }
    const formatted = [header, ...this.#labels.map((label, index) => label + values[index])].join('\n');
    this.#rows.push(formatted);
    this.#characters += separator + formatted.length;
    this.#formatted = undefined;
    return true;
  }

  format() {
    return this.#formatted ??= this.#rows.join('\n');
  }
}

export function formatTable(columns, rows) {
  const formatter = new TableFormatter(columns);
  if (!Array.isArray(rows)) throw new TypeError('Table rows must be an array.');
  for (const row of rows) formatter.append(row);
  return formatter.format();
}
