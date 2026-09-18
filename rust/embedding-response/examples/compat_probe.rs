// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
// Line protocol for tests/compare_cpp.py: dimension encoding hex-response.
use embedding_response::{parse_into, Encoding, ParseError};
use std::io::{self, BufRead, Write};

fn main() -> io::Result<()> {
    let mut stdout = io::BufWriter::new(io::stdout().lock());
    for line in io::stdin().lock().lines() {
        let line = line?;
        let mut fields = line.split_whitespace();
        let dimension = fields.next().unwrap().parse().unwrap();
        let encoding = if fields.next().unwrap() == "b" {
            Encoding::Base64
        } else {
            Encoding::Float
        };
        let hex = fields.next().unwrap_or("");
        let bytes: Vec<_> = (0..hex.len())
            .step_by(2)
            .map(|i| u8::from_str_radix(&hex[i..i + 2], 16).unwrap())
            .collect();
        let mut output = vec![vec![42.0]];
        let code = parse_into(&bytes, dimension, encoding, &mut output)
            .map_or_else(ParseError::ob_error_code, |()| 0);
        write!(stdout, "{code} {}", output.len())?;
        for vector in output {
            write!(stdout, " {}", vector.len())?;
            for value in vector {
                write!(stdout, " {:08x}", value.to_bits())?;
            }
        }
        writeln!(stdout)?;
    }
    Ok(())
}
