#!/usr/bin/env python3
"""
Interactive SQL Client for SeekDB

A command-line tool for executing SQL commands interactively, similar to mysql client.
Works with embedded SeekDB databases.

Usage:
    python interactive_sql_client.py [--path PATH] [--database DATABASE]
    python interactive_sql_client.py -p ./seekdb -d test
"""
import sys
import argparse
import signal
import os
from typing import Optional, List, Any, Tuple

# Try to import readline for history support (Unix/Linux/Mac)
try:
    import readline
    HAS_READLINE = True
except ImportError:
    # Windows doesn't have readline by default, try pyreadline
    try:
        import pyreadline3 as readline
        HAS_READLINE = True
    except ImportError:
        HAS_READLINE = False
        if sys.platform != 'win32':
            print("Warning: readline module not available. History navigation disabled.")

try:
    import tabulate
    HAS_TABULATE = True
except ImportError:
    HAS_TABULATE = False

try:
    import pylibseekdb as seekdb
except ImportError:
    print("Error: seekdb module not found. Please install it first.")
    sys.exit(1)


def _special_commands_string() -> str:
        return """
Special commands:
    \\q, \\quit, \\exit    - Exit the client
    \\d [table]            - Describe table structure
    \\dt                   - List all tables
    \\l, \\databases        - List all databases
    \\c [database]         - Connect to a different database
    \\h, \\help, \\?         - Show this help message
        """
class InteractiveSQLClient:
    """Interactive SQL client for SeekDB"""

    def __init__(self, path: str = "./seekdb", database: str = "test"):
        """
        Initialize the interactive SQL client

        Args:
            path: Path to SeekDB data directory
            database: Database name to connect to
        """
        self.path = path
        self.database = database
        self.connection = None
        self.cursor: Optional[Any] = None
        self.running = True
        self.history_file = os.path.expanduser("~/.seekdb_history")
        self.sql_history: List[str] = []

        # Initialize readline if available
        if HAS_READLINE:
            self._setup_readline()

        # Register signal handlers for graceful exit
        signal.signal(signal.SIGINT, self._handle_sigint)
        signal.signal(signal.SIGTERM, self._handle_sigterm)

    def _handle_sigint(self, signum, frame):
        """Handle Ctrl+C gracefully"""
        print("\nUse \\q or \\quit to exit.")
        self.running = True  # Don't exit on Ctrl+C, just show message

    def _handle_sigterm(self, signum, frame):
        """Handle termination signal"""
        self.cleanup()
        sys.exit(0)

    def _setup_readline(self):
        """Setup readline for history and better input handling"""
        if not HAS_READLINE:
            return

        # Set history file
        try:
            if os.path.exists(self.history_file):
                readline.read_history_file(self.history_file)
        except Exception:
            pass

        # Configure readline
        # Use vi mode if preferred, or keep default emacs mode
        # readline.parse_and_bind('set editing-mode vi')  # Uncomment for vi mode

        # Set history length
        readline.set_history_length(1000)

        # Optional: Set completer for tab completion (can be enhanced later)
        # readline.set_completer(self._completer)
        # readline.parse_and_bind('tab: complete')

    def _save_history(self):
        """Save command history to file"""
        if not HAS_READLINE:
            return

        try:
            readline.write_history_file(self.history_file)
        except Exception:
            pass

    def connect(self) -> bool:
        """Connect to the database"""
        try:
            print("Openning database...")
            seekdb.open(db_dir=self.path)
            self.connection = seekdb.connect(database=self.database, autocommit=True)
            self.cursor = self.connection.cursor()
            # Test connection
            self.cursor.execute("SELECT 1")
            return True
        except Exception as e:
            print(f"Error connecting to database: {e}")
            return False

    def cleanup(self):
        """Close connection and cleanup"""
        # Save history before exiting
        if HAS_READLINE:
            self._save_history()

        if self.cursor:
            try:
                self.cursor.close()
            except Exception:
                pass
            self.cursor = None
        if self.connection:
            try:
                self.connection.close()
            except Exception:
                pass
            self.connection = None

    def _format_simple_table(self, headers: List[str], rows: List[List[Any]]) -> str:
        """
        Format table without tabulate library

        Args:
            headers: Column headers
            rows: Table rows

        Returns:
            Formatted table string
        """
        if not headers or not rows:
            return "(empty result set)"

        # Calculate column widths
        col_widths = [len(str(h)) for h in headers]
        for row in rows:
            for i, cell in enumerate(row):
                if i < len(col_widths):
                    col_widths[i] = max(col_widths[i], len(str(cell)))

        # Build table
        lines = []

        # Header row
        header_line = " | ".join(str(h).ljust(col_widths[i]) for i, h in enumerate(headers))
        lines.append(header_line)
        lines.append("-" * len(header_line))

        # Data rows
        for row in rows:
            row_line = " | ".join(str(cell).ljust(col_widths[i]) if i < len(col_widths) else str(cell)
                                 for i, cell in enumerate(row))
            lines.append(row_line)

        return "\n".join(lines)

    def _extract_column_names_from_sql(self, sql: str) -> Optional[List[str]]:
        """
        Try to extract column names from SQL SELECT statement

        Args:
            sql: SQL SELECT statement

        Returns:
            List of column names or None if extraction fails
        """
        import re
        try:
            # Match SELECT ... FROM pattern
            select_match = re.search(r'SELECT\s+(.+?)\s+FROM', sql, re.IGNORECASE | re.DOTALL)
            if not select_match:
                return None

            select_clause = select_match.group(1).strip()

            # Handle SELECT * case
            if select_clause.strip() == '*':
                return None

            # Split by comma, handling nested parentheses
            parts = []
            depth = 0
            current = ""
            for char in select_clause:
                if char == '(':
                    depth += 1
                elif char == ')':
                    depth -= 1
                elif char == ',' and depth == 0:
                    parts.append(current.strip())
                    current = ""
                    continue
                current += char
            if current:
                parts.append(current.strip())

            # Extract column names
            column_names = []
            for part in parts:
                part = part.strip()
                # Match "AS alias" pattern
                as_match = re.search(r'\s+AS\s+["\']?(\w+)["\']?', part, re.IGNORECASE)
                if as_match:
                    column_names.append(as_match.group(1))
                else:
                    # No alias, extract column name
                    # Remove backticks and quotes, get last identifier
                    cleaned = part.replace('`', '').replace('"', '').replace("'", '')
                    # Get last word (column name)
                    words = cleaned.split()
                    if words:
                        col_name = words[-1]
                        # Remove function parentheses if present
                        if '(' in col_name:
                            col_name = col_name.split('(')[0]
                        column_names.append(col_name if col_name else f"col_{len(column_names) + 1}")
                    else:
                        column_names.append(f"col_{len(column_names) + 1}")

            return column_names if column_names else None
        except Exception:
            return None

    def format_result(self, result: Any, sql: Optional[str] = None) -> str:
        """
        Format query result as a table

        Args:
            result: Query result from execute()
            sql: Original SQL statement (optional, used to extract column names)

        Returns:
            Formatted table string
        """
        if not result:
            return "(empty result set)"

        # Handle different result formats
        if isinstance(result, (list, tuple)):
            if len(result) == 0:
                return "(empty result set)"

            # Check if result is list of tuples or list of dicts
            first_row = result[0]

            if isinstance(first_row, dict):
                # Result is list of dictionaries
                headers = list(first_row.keys())
                rows = [[row.get(h, '') for h in headers] for row in result]
            elif isinstance(first_row, (tuple, list)):
                # Result is list of tuples
                num_cols = len(first_row)

                # Try to extract column names from SQL
                headers = None
                if sql:
                    headers = self._extract_column_names_from_sql(sql)

                # Fallback to generic column names
                if not headers or len(headers) != num_cols:
                    headers = [f"col_{i+1}" for i in range(num_cols)]

                rows = [list(row) for row in result]
            else:
                # Single value result
                return str(result)

            # Format with tabulate if available
            if HAS_TABULATE:
                try:
                    return tabulate.tabulate(rows, headers=headers, tablefmt="grid")
                except Exception:
                    pass

            # Fallback to simple table format
            return self._format_simple_table(headers, rows)
        else:
            return str(result)

    def execute_sql(self, sql: str) -> Tuple[bool, str]:
        """
        Execute SQL statement

        Args:
            sql: SQL statement to execute

        Returns:
            Tuple of (success: bool, result_message: str)
        """
        if not self.cursor:
            return False, "Not connected to database"

        try:
            sql_upper = sql.strip().upper()

            # Execute the SQL
            self.cursor.execute(sql)
            result = self.cursor.fetchall()

            # Format result based on query type
            if sql_upper.startswith('SELECT') or sql_upper.startswith('SHOW') or sql_upper.startswith('DESC'):
                formatted = self.format_result(result, sql=sql)
                return True, formatted
            else:
                # DML/DDL statements
                if hasattr(result, 'rowcount'):
                    return True, f"Query OK, {result.rowcount} row(s) affected"
                else:
                    return True, "Query OK"

        except Exception as e:
            return False, f"Error: {str(e)}"

    def handle_special_command(self, command: str) -> Tuple[bool, Optional[str]]:
        """
        Handle special commands (starting with \)

        Args:
            command: Special command string

        Returns:
            Tuple of (handled: bool, message: Optional[str])
        """
        command = command.strip()

        if not command.startswith('\\'):
            return False, None

        parts = command.split(None, 1)
        cmd = parts[0].lower()
        args = parts[1] if len(parts) > 1 else None

        if cmd in ('\\q', '\\quit', '\\exit'):
            self.running = False
            return True, "Bye!"

        elif cmd in ('\\h', '\\help', '\\?'):
            help_text = _special_commands_string() + """
SQL commands:
    Enter SQL statements terminated by semicolon (;)
    Multi-line statements are supported.

History navigation:
    Use Up/Down arrow keys to navigate through command history
    History is saved to ~/.seekdb_history
            """
            return True, help_text.strip()

        elif cmd == '\\d':
            # Describe table
            if not args:
                return True, "Usage: \\d <table_name>"

            table_name = args.strip().strip('`').strip('"').strip("'")
            success, result = self.execute_sql(f"DESCRIBE `{table_name}`")
            return True, result if success else result

        elif cmd == '\\dt':
            # List tables
            success, result = self.execute_sql("SHOW TABLES")
            return True, result if success else result

        elif cmd in ('\\l', '\\databases'):
            # List databases
            success, result = self.execute_sql(
                "SELECT SCHEMA_NAME FROM information_schema.SCHEMATA"
            )
            return True, result if success else result

        elif cmd == '\\c':
            # Connect to different database
            if not args:
                return True, f"Current database: {self.database}\nUsage: \\c <database_name>"

            new_db = args.strip().strip('`').strip('"').strip("'")
            try:
                self.cleanup()
                self.database = new_db
                if self.connect():
                    return True, f"Connected to database: {new_db}"
                else:
                    return True, f"Failed to connect to database: {new_db}"
            except Exception as e:
                return True, f"Error: {str(e)}"

        else:
            return True, f"Unknown command: {cmd}. Type \\h for help."

    def read_multiline_sql(self) -> Optional[str]:
        """
        Read multi-line SQL statement from user input with readline support

        Returns:
            Complete SQL statement or None if user wants to quit
        """
        lines = []
        prompt = "seekdb[embedded] > "
        continuation_prompt = "    -> "

        while True:
            try:
                # input() automatically uses readline if available (supports up/down arrows)
                if lines:
                    line = input(continuation_prompt)
                else:
                    line = input(prompt)
            except EOFError:
                # User pressed Ctrl+D
                print("\nBye!")
                return None
            except KeyboardInterrupt:
                # User pressed Ctrl+C - clear current input
                print("\n(Interrupted)")
                if lines:
                    lines = []
                    continue
                else:
                    print("Use \\q or \\quit to exit.")
                    continue

            if not line.strip():
                # Empty line - in multi-line mode, this might be intentional
                # In single-line mode, skip it
                if not lines:
                    continue
                # In multi-line mode, empty line might be part of the statement
                # or user wants to cancel - let's treat it as cancel for now
                # User can type semicolon to complete
                continue

            # Check for special commands
            if line.strip().startswith('\\'):
                return line.strip()

            lines.append(line)

            # Check if statement is complete (ends with semicolon)
            if line.rstrip().endswith(';'):
                sql = '\n'.join(lines)
                # Keep the semicolon - most SQL databases accept it
                return sql.strip()

    def run(self):
        """Run the interactive SQL client"""
        print("=" * 60)
        print("OceanBase SeekDB Embedded Interactive SQL Client")
        print("=" * 60)
        print(f"Path: {self.path}")
        print(f"Database: {self.database}")
        print("=" * 60)
        print("Type '\\h' for help or '\\q' to quit.")
        print()

        if not self.connect():
            print("Failed to connect. Exiting.")
            return

        while self.running:
            try:
                sql = self.read_multiline_sql()

                if sql is None:
                    break

                if not sql.strip():
                    continue

                # Check for special commands
                if sql.strip().startswith('\\'):
                    handled, message = self.handle_special_command(sql)
                    if message:
                        print(message)
                    if not self.running:
                        break
                    continue

                # Execute SQL
                success, result = self.execute_sql(sql)
                print(result)

                # Add to history if successful (for readline)
                if success and HAS_READLINE:
                    # Add the original SQL to readline history
                    # This allows up/down arrow navigation
                    try:
                        readline.add_history(sql)
                    except Exception:
                        pass

                if not success:
                    # Error occurred, but continue running
                    pass

                print()  # Empty line for readability

            except KeyboardInterrupt:
                print("\nUse \\q or \\quit to exit.")
                continue
            except Exception as e:
                print(f"Unexpected error: {e}")
                continue

        self.cleanup()
        print("Connection closed.")


def main():
    """Main entry point"""
    parser = argparse.ArgumentParser(
        description="Interactive SQL client for OceanBase SeekDB Embedded",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
    %(prog)s
    %(prog)s --path ./seekdb --database test
    %(prog)s -p ./seekdb -d mydb

        """ + _special_commands_string()
    )

    parser.add_argument(
        '-p', '--path',
        default='./seekdb',
        help='Path to SeekDB data directory (default: ./seekdb)'
    )

    parser.add_argument(
        '-d', '--database',
        default='test',
        help='Database name (default: test)'
    )

    args = parser.parse_args()

    client = InteractiveSQLClient(path=args.path, database=args.database)
    client.run()


if __name__ == "__main__":
    main()
