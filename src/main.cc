#include <iostream>
#include <vector>
#include <string>
#include <tuple>
#include <bitset>
#include <fmt/core.h>
#include "vendor/json.hh"

using namespace std;
using json = nlohmann::json;

string unicode_slice(const string& s) {
    const char* buffer = s.c_str();

    size_t start_offset = 0; 
    size_t end_offset = s.size() - 1;

    while(((buffer[start_offset]) & 0xc0) == 0x80) start_offset++;
    while(((buffer[end_offset]) & 0xc0) == 0x80) end_offset--;

    if (end_offset < start_offset) {
        throw "Invalid unicode";
    }

    // cout << std::bitset<8>(buffer[start_offset]) << " " << std::bitset<8>((buffer[start_offset]) & 0xc0) << endl;
    // cout << std::bitset<8>(buffer[end_offset]) << " " << std::bitset<8>((buffer[end_offset]) & 0xc0) << endl;

    return string(&buffer[start_offset], end_offset-start_offset);
}

string extract_context(const char* buffer, size_t size, size_t pos) {
    const size_t context_size = 20;

    size_t start_pos = pos > context_size ? pos - context_size : 0; 
    size_t end_pos = min(pos + context_size, size);

    return unicode_slice(string(&buffer[start_pos], end_pos-start_pos));
}

namespace ns {

struct parse_state {
    bool valid;
    size_t cell;
    size_t cells_first_row;
    size_t row;
    size_t bytes;
    bool escape_next;
    bool quote_active;
    bool cr; // if record terminator = CR LF
    bool current_field_had_quote;
    bool has_header;
    bool current_field_is_not_header;
    bool prev_field_is_not_header;
    bool cell_numeric;
    bool cell_empty;
    
    // features
    bool fix_broken_line_breaks; // concat rows on column amount mismatch
    bool escape_line_breaks;     // escape char escapes line break
    bool allow_mixed_quoting;    // allow quoted and unquoted data in one field

    // dialect
    char field_terminator;
    char record_terminator;
    char quote_char;
    char escape_char;
    vector<bool> column_empty;
    vector<bool> column_numeric;

    // dialect rejection reason
    string error_desc;

    parse_state(char field_terminator, char record_terminator, char quote_char, char escape_char): 
        field_terminator(field_terminator),
        record_terminator(record_terminator),
        quote_char(quote_char),
        escape_char(escape_char),
        valid(true),
        cell(1),
        row(0),
        bytes(0),
        cells_first_row(0),
        escape_next(false),
        quote_active(false),
        cr(false),
        current_field_had_quote(false),
        fix_broken_line_breaks(false),
        escape_line_breaks(true),
        error_desc(""),
        allow_mixed_quoting(false),
        has_header(true),
        current_field_is_not_header(false),
        prev_field_is_not_header(false),
        cell_empty(true),
        cell_numeric(true),
        column_empty({}),
        column_numeric({})
    {};

    void consume_many(char* buffer, size_t amount) {
        for (size_t pos = 0; pos < amount; ++pos) {
            char c = buffer[pos];

            if (!this->valid) return;
            this->bytes++;

            // if (
            //     this->row > 1000
            //     && c == '\n'
            //     && this->quote_active 
            // ) {
            //     printf("chunk: %.*s\n", amount, buffer);
            // }

            // check for CRLF line break, also considering:
            // - quote state 
            // - escape state and escape_line_breaks flag
            // - current cells in a row and fix_broken_line_breaks flag
            if (
                this->record_terminator == '\r' 
                && this->cr 
                && c == '\n' 
                && !this->quote_active 
                && !(this->escape_line_breaks && this->escape_next) ) 
            {
                if (this->row == 0) {
                    if (this->cell == 1) {
                        this->valid = false;
                        this->error_desc = fmt::format("At row {}, cell {}, near >>>{}<<<: Only one column found", 
                            this->row, 
                            this->cell,
                            extract_context(buffer, amount, pos)
                        ); 
                        return;
                    }
                    // header detection
                    this->has_header = this->has_header && !this->prev_field_is_not_header;

                    this->cells_first_row = this->cell;
                    this->column_empty.resize(this->cells_first_row, true);
                    this->column_numeric.resize(this->cells_first_row, true);
                } else if (this->cells_first_row != this->cell) {
                    // here goes fix_broken_line_breaks

                    this->valid = false;
                    this->error_desc = fmt::format("At row {}, cell {}, near '{}': Inconsistent column amount", 
                        this->row, 
                        this->cell, 
                        extract_context(buffer, amount, pos)
                    ); 
                    return;
                }
                this->cr = false;
                this->cell = 1;
                this->escape_next = false;
                this->current_field_had_quote = false;
                this->current_field_is_not_header = false;
                this->row++;
                continue;
            }
            if (
                this->record_terminator == '\r'
                && c == '\r'
            ) {
                this->cr = true;
                continue;
            }

            // handle LF line break
            if (
                this->record_terminator == '\n'
                && c == '\n'
                && !this->quote_active 
                && !(this->escape_line_breaks && this->escape_next)
            ) {
                if (this->row == 0) {
                    if (this->cell == 1) {
                        this->valid = false;
                        this->error_desc = fmt::format("At row {}, cell {}, near >>>{}<<<: Only one column found", 
                            this->row, 
                            this->cell,
                            extract_context(buffer, amount, pos)
                        ); 
                        return;
                    }
                    // header detection
                    this->has_header = this->has_header && !this->prev_field_is_not_header;
                    
                    this->cells_first_row = this->cell;
                    this->column_empty.resize(this->cells_first_row, true);
                    this->column_numeric.resize(this->cells_first_row, true);
                } else if (this->cells_first_row != this->cell) {
                    // here goes fix_broken_line_breaks
                    this->error_desc = fmt::format("At row {}, cell {}, near '{}': Inconsistent column amount", 
                            this->row, 
                            this->cell, 
                            extract_context(buffer, amount, pos)
                    ); 
                    this->valid = false;
                    return;
                }
                this->cell = 1;
                this->escape_next = false;
                this->current_field_had_quote = false;
                this->row++;
                continue;
            }
            if (
                this->record_terminator == '\n'
                && c == '\r'
            ) {
                this->valid = false;
                this->error_desc = fmt::format("At row {}, cell {}, near '{}': CR detected, seems like CRLF line breaks", 
                    this->row, 
                    this->cell, 
                    extract_context(buffer, amount, pos)
                ); 
                return;
            }

            // handle escape
            if (
                this->escape_char == c 
                && this->escape_char != this->quote_char
                && !this->escape_next
            ) {
                this->escape_next = true;
                continue;
            }

            // handle quote
            if (
                this->quote_char == c
            ) {
                this->quote_active = !this->quote_active;
                this->current_field_had_quote = true;
                continue;
            }

            // handle field break
            if (
                this->field_terminator == c
                && !this->escape_next
                && !this->quote_active
            ) {
                // header detection
                if (
                    this->row == 0 
                    && this->has_header
                ) {
                    if (this->prev_field_is_not_header ) {
                        this->has_header = false;
                    }
                    this->prev_field_is_not_header = this->current_field_is_not_header;
                    if (this->cell_empty) {
                        this->prev_field_is_not_header = true;
                    }
                }

                // field statistics
                if (this->row != 0 && this->cell <= this->cells_first_row) {
                    this->column_empty[this->cell - 1] = this->column_empty[this->cell - 1] && this->cell_empty;
                    this->column_numeric[this->cell - 1] = this->column_numeric[this->cell - 1] && this->cell_numeric;
                }
                
                this->current_field_is_not_header = false;
                this->current_field_had_quote = false;
                this->cell++;
                continue;
            }

            if (this->current_field_had_quote && !this->quote_active) {
                this->valid = false;
                this->error_desc = fmt::format("At row {}, cell {}, near '{}': Mixed quoting detected", 
                    this->row, 
                    this->cell, 
                    extract_context(buffer, amount, pos)
                ); 
                return;
            }

            // not header if:
            // 1. first row contains: "!#$%&()*+,:-/;M<=>?[]^{}~|`@" or space
            // 2. column name length <= 1
            // header if: 
            // 1. column is numeric except first field
            // 2. column is empty except first field
            if (
                this->row == 0 
                && (c < '0' || (c > '9' && c < 'A') || (c > 'Z' && c < '_') || (c > 'z' && c <= '~'))
                && ((c & 0x80) != 0x80)
            ) {
                this->current_field_is_not_header = true;
            }

            if (c < '0' || c > '9') {
                this->cell_numeric = false;
            }

            this->cell_empty = false;
            this->escape_next = false;
            this->cr = false;
        }
    };
};

void to_json(json& j, const parse_state& p) {
    j = json {
        {
            "dialect", {
                {"field_terminator", string(1, p.field_terminator)},
                {"record_terminator", string(1, p.record_terminator)},
                {"quote_char", string(1, p.quote_char)},
                {"escape_char", string(1, p.escape_char)},
                {"has_header", p.has_header},
                {"fields", p.cells_first_row},
                {"column_empty", p.column_empty},
                {"column_numeric", p.column_numeric}
            }
        },
        {
            "features", {
                {"fix_broken_line_breaks", p.fix_broken_line_breaks},
                {"escape_line_breaks", p.escape_line_breaks},
                {"allow_mixed_quoting", p.allow_mixed_quoting}
            }
        },
        {
            "stats", {
                {"rows", p.row},
                {"bytes", p.bytes}
            }
        },
        {
            "error_description", 
            p.error_desc
        },
        {
            "valid",
            p.valid
        }
    };
}

}

vector<ns::parse_state> filter_simple_candidates(vector<ns::parse_state> all_states) {
    vector<ns::parse_state> result;
    std::copy_if (all_states.begin(), all_states.end(), std::back_inserter(result), [](const ns::parse_state& a){
        return a.row > 2;
    });
    return result;
}


int main(int argc, char** argv) {
    // 1. Read huge chunk from stdin
    // 2. Try to parse it as CSV until correct dialect is found

    vector<char> field_terminators = {'\t', ',', ';', '|'};
    vector<char> record_terminators = {'\r', '\n'};
    vector<ns::parse_state> parse_states = {}; 
    
    for (auto &ft: field_terminators) {
        for(auto &rt: record_terminators) {
            parse_states.push_back(ns::parse_state(ft, rt, '"', '"'));
            parse_states.push_back(ns::parse_state(ft, rt, '"', '\\'));
            parse_states.push_back(ns::parse_state(ft, rt, '\'', '\\'));
            parse_states.push_back(ns::parse_state(ft, rt, '\'', '\0')); // no escape
            parse_states.push_back(ns::parse_state(ft, rt, '"', '\0')); // no escape
        }
    }

    const int buffer_size = 65536;
    char buffer[buffer_size];
    size_t buffer_read_size = 0;
    
    while (buffer_read_size = fread(buffer, sizeof(char), buffer_size, stdin)) {
        int valid_parsers = 0;
        for (auto& parser: parse_states) {
            if (parser.valid) {
                parser.consume_many(buffer, buffer_read_size);
                if (parser.valid) {
                    valid_parsers++;
                }
            }
        }
        if (valid_parsers == 0) {
            cout << json {
                {"result", "not_valid"},
                {"candidates", filter_simple_candidates(parse_states)}
            };
            return 0;
        }
    }
    if (ferror(stdin)){
        perror("Error");
        return 1;
    }

    vector<ns::parse_state> valid_states;
    std::copy_if (parse_states.begin(), parse_states.end(), std::back_inserter(valid_states), [](const ns::parse_state& a){
        return a.valid;
    });

    if (valid_states.size() == 1) {
        cout << json {
            {"result", "valid"},
            {"detected", valid_states[0]},
            {"candidates", filter_simple_candidates(parse_states)}
        };
    } else {
        cout << json {
            {"result", "ambigous"},
            {"candidates", filter_simple_candidates(parse_states)}
        };
    }

    return 0;
}
