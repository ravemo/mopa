#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <assert.h>
#include <stx/stx.c>
typedef size_t usize;

void breakpoint() {}
FILE* file = NULL;
void createFile(const char* name) {
	file = fopen(name, "w");
	assert(file != NULL);
}
void closeFile() {
	fclose(file);
	file = NULL;
}

int indent = 0;
void printRaw(const char* fmt, ...) {
	va_list vargs;
	va_start(vargs, fmt);
	vfprintf(file, fmt, vargs);
	va_end(vargs);
}
void print(const char* fmt, ...) {
	va_list vargs;
	assert(indent >= 0);
	for(int i = 0; i < indent; ++i) fprintf(file, "\t");
	va_start(vargs, fmt);
	vfprintf(file, fmt, vargs);
	va_end(vargs);
}
void println(const char* fmt, ...) {
	va_list vargs;
	assert(indent >= 0);
	for(int i = 0; i < indent; ++i) fprintf(file, "\t");
	va_start(vargs, fmt);
	vfprintf(file, fmt, vargs);
	fprintf(file, "\n");
	va_end(vargs);
}

typedef enum {
	Struct, // or union
	Enum,
	Array,
} ColumnKind;

typedef struct {
	ColumnKind kind;
	bool is_internal;
	bool is_hidden;
	bool emit_x_macro;
	const char* type;
	const char* name;
	CStringArray rows;
} Column;
	DEFINE_ARRAY(Column, ColumnArray)
IMPLEMENT_ARRAY(Column, ColumnArray)

	typedef struct {
		const char* name;
		ColumnArray columns;
	} Table;
	DEFINE_ARRAY(Table, TableArray)
IMPLEMENT_ARRAY(Table, TableArray)

int find(char needle, const char* heap, int len, int start) {
	int i;
	for(i = start; i < len && heap[i] != needle; ++i);
	return i;
}
int findDelimiter(const char* heap, int len, int start) {
	int i;
	bool open_quotes = false;
	int open_braces = 0;
	int open_parens = 0;
	for(i = start; i < len; ++i) {
		if(heap[i] == '"') open_quotes = !open_quotes;
		else if(!open_quotes) {
			if(heap[i] == '{') ++open_braces;
			else if(heap[i] == '}') --open_braces;
			else if(heap[i] == '(') ++open_parens;
			else if(heap[i] == ')') --open_parens;
			else if(heap[i] == ',' && !open_braces && !open_parens) break;
		}
	}
	return i;
}
int findClosingParens(const char* heap, int len, int start) {
	int open_parens = 1; // assuming start is after the opening parens
	bool open_quotes = false;
	for(int i = start; i < len; ++i) {
		if(heap[i] == '(') ++open_parens;
		else if(heap[i] == ')') --open_parens;
		else if(heap[i] == '"') open_quotes = !open_quotes;
		if(!open_parens && !open_quotes) return i;
	}
	assert(false);
}
int findEntryStart(const char* heap, int len, int start) {
	int i;
	for(i = start; i < len; ++i)
		if(heap[i] != ' ' && heap[i] != '\t' && heap[i] != ',') break;
	return i;
}
int trimSpacesAtEnd(const char* heap, int end, int start) {
	int new_end = end;
	for(int i = end-1; i >= start; --i) {
		if(isspace(heap[i])) new_end = i;
		else break;
	}
	return new_end;
}

typedef enum {Keep, Upper, Lower} CaseTransform;
char transformCase(char c, CaseTransform transform) {
	switch(transform) {
		case Keep: return c;
		case Upper: return toupper(c);
		case Lower: return tolower(c);
	}
	UNREACHABLE;
}

char buf[2048];
StringView replaceReferencesInEntry(StringView original_entry, Table* table, int row) {
	int pos = 0;
	for(usize i = 0; i < original_entry.len; ++i){
		if(original_entry.ptr[i] == '\\') {
			const char *substr = original_entry.ptr+i+1;
			bool has_case_specifier = false;
			CaseTransform first_case = Keep;
			CaseTransform word_case = Keep;
			CaseTransform tail_case = Keep;
			enum {Space, Underscore, Delete} space_transform = Space;

			switch(substr[0]) {
				case '\\': // escaped backslash
					buf[pos++] = original_entry.ptr[i++];
				continue;
				case '_':
					// separator, in case you want to append a number after a backreference,
					// like this: `abc, \0\_1` -> `abc, abc1`
					++i;
				continue;
#define Case(c, first, word, tail, space) case c: \
	first_case = first; word_case = word; tail_case = tail; space_transform = space; has_case_specifier = true; \
	++substr; break;
				Case('l', Lower, Lower, Lower, Space); // lower case
				Case('u', Upper, Upper, Upper, Space); // UPPER CASE
				Case('s', Lower, Lower, Lower, Underscore); // snake_case
				Case('c', Lower, Upper, Lower, Delete); // camelCase
				Case('p', Upper, Upper, Lower, Delete); // PascalCase
				Case('m', Upper, Upper, Upper, Underscore); // MACRO_CASE
#undef Case
			}
			if(!isdigit(substr[0])) continue;
			usize digit_count = 0;
			usize referenced_col = 0;
			for(digit_count = 0; isdigit(substr[digit_count]); ++digit_count) {
				referenced_col *= 10;
				referenced_col += substr[digit_count] - '0';
			}
			assert(referenced_col < table->columns.len);
			const char* referenced = table->columns.items[referenced_col].rows.items[row-2];
			usize out_len = 0;
			bool word_start = false;
			for(usize j = 0; j < strlen(referenced); ++j, ++out_len) {
				if(referenced[j] == ' ') {
					switch(space_transform) {
						case Space: buf[pos+out_len] = ' '; break;
						case Underscore: buf[pos+out_len] = '_'; break;
						case Delete: --out_len; break;
					}
					word_start = true;
					continue;
				} else if(j == 0) buf[pos+out_len] = transformCase(referenced[j], first_case);
				else if(word_start) buf[pos+out_len] = transformCase(referenced[j], word_case);
				else buf[pos+out_len] = transformCase(referenced[j], tail_case);
				word_start = false;
			}
			pos += out_len;
			i += digit_count;
			if(has_case_specifier) i += 1;
			continue;
		}
		buf[pos++] = original_entry.ptr[i];
	}
	buf[pos] = 0;
	return (StringView) {.ptr = buf, .len = pos};
}

const char* getSizeMacro(Table t, Column col, const char* table_size_macro, usize *row_count) {
	for(*row_count = col.rows.len; *row_count < col.rows.len; --*row_count) {
		if(strcmp(col.rows.items[*row_count-1], "-") != 0) break;
	}

	const char* size_macro = table_size_macro;
	if(*row_count != col.rows.len) {
		size_macro = tprint("%s_%s_COUNT", t.name, col.name);
		println("#define %s %d", size_macro, *row_count);
	}
	return size_macro;
}

void printColumnInternals(Column col, usize row_count) {
	char separator = col.kind == Struct ? ';' : ',';
	for(usize k = 0; k < row_count; ++k){
		if(strcmp(col.rows.items[k], "-") == 0) continue;
		print("%s", col.rows.items[k]);
		if(separator == ';' || k < row_count-1) printRaw("%c \\\n", separator);
		else printRaw(" \\\n");
	}
}

void genTables(const char* src_dir) {
	Allocator gpa = initArena();
	TableArray tables = {0};
	// Parse all tables in the source files
	// TODO: Maybe parallelize this
	DIR* cwd = opendir(src_dir);
	struct dirent* dir_entry;
	const char* prefix = "CREATE_TABLE_";
	while((dir_entry = readdir(cwd)) != NULL) {
		const char* name = dir_entry->d_name;
		int name_len = strlen(name);
		if(strcmp(name, "autogenerated_tables.h") == 0) continue;
		if(strcmp(name + name_len - 2, ".c") != 0 && strcmp(name + name_len - 2, ".h") != 0) continue;
		const char* path = tprint("%s/%s", src_dir, name);
		struct stat stbuf;
		if(stat(path, &stbuf) == -1) {
			fprintf(stderr, "Unable to stat file: %s\n", path);
			exit(1);
		}
		if(!S_ISREG(stbuf.st_mode)) continue;
		char* mem = readFileContents(path);
		int mem_size = strlen(mem);

		for(int pos = 0; mem[pos]; ++pos) {
			if(strncmp(mem+pos, prefix, strlen(prefix)) != 0) continue;
			appendTable(&tables, gpa, (Table){0});
			Table *table = &LAST(tables);
			int start = pos+strlen(prefix);
			int open_paren = find('(', mem, mem_size, start);
			table->name = allocPrintZ(gpa, "%.*s", open_paren-pos-(int)strlen(prefix), mem+pos+strlen(prefix));
			int close_paren = findClosingParens(mem, mem_size, open_paren+1);

			// Clean off comments to help with parsing
			for(int i = open_paren+1; i < close_paren-1; ++i) {
				if(mem[i] == '/' && mem[i+1] == '/') {
					int line_end = find('\n', mem, close_paren, i);
					for(int j = i; j < line_end; ++j) mem[j] = ' ';
				}
				if(mem[i] == '/' && mem[i+1] == '*') {
					int comment_end;
					for(comment_end = i+2; comment_end < close_paren-1; ++comment_end) {
						if(mem[comment_end] == '*' && mem[comment_end+1] == '/') break;
					}
					for(int j = i; j < comment_end+1; ++j) mem[j] = ' ';
				}
			}

			pos = open_paren+1;
			int column_count = -1;
			for(int row_count = 0; ; ++row_count) {
				int line_start = 1+find('\n', mem, close_paren, pos);
				int line_end = find('\n', mem, close_paren, line_start);
				if(line_end == close_paren) break;
				int entry_end = line_start;
				int col_count;
				for(col_count = 0; ; ++col_count) {
					if(entry_end == line_end) break;
					int entry_start = findEntryStart(mem, line_end, entry_end+1);
					if(entry_start == line_end || (mem_size - entry_start >= 3 && strncmp(mem+entry_start, "---", 3) == 0)) {
						entry_end = line_end;
						break;
					}
					// TODO: Support commas inside entries (eg inside a parens)
					entry_end = findDelimiter(mem, line_end, entry_start);
					entry_end = trimSpacesAtEnd(mem, entry_end, entry_start);
					StringView original_entry = (StringView){.ptr = mem + entry_start, .len = entry_end - entry_start};
					StringView entry = replaceReferencesInEntry(original_entry, table, row_count);
					
					if(column_count == -1) {
						appendColumn(&table->columns, gpa, (Column) {0});
					} else assert(col_count < (signed)table->columns.len);
					Column *col = &table->columns.items[col_count];
					if(row_count == 0) {
						// First row is for types
						StringView type = entry;
						type.len = find('@', type.ptr, entry.len, 0);
						StringView attr = (StringView){.ptr = type.ptr+type.len+1, .len = 0};
						if(type.len < entry.len) attr.len = entry.len-type.len-1;
						if(0) {
					 } else if(viewEql(type, span("enum"))) {
							col->kind = Enum;
						} else if(viewEql(type, span("statement"))) {
							col->kind = Struct;
							col->is_internal = true;
						} else if(viewEql(type, span("struct")) || viewEql(type, span("union"))) {
							col->kind = Struct;
						} else if(viewEql(type, span("-"))) {
							col->is_hidden = true;
						} else { // array of type
							col->kind = Array;
						}
						if(type.len < entry.len) breakpoint();
						if(attr.len == 0) {
					 } else if(viewEql(attr, span("internal"))) {
							col->is_internal = true;
						} else if(viewEql(attr, span("X"))) {
							col->emit_x_macro = true;
						} else {
							fprintf(stderr, "Unhandled attribute '%.*s'\n", SVA(attr));
							exit(1);
						}
						col->type = allocPrintZ(gpa, "%.*s", SVA(type));
					} else if(row_count == 1) {
						// Second row is for variable names
						// TODO: Support anonymous structs/unions/enums
						col->name = allocPrintZ(gpa, "%.*s", SVA(entry));
					} else {
						// The remaining rows are entries in the table
						char* cloned_entry = allocPrintZ(gpa, "%.*s", SVA(entry));
						appendCString(&col->rows, gpa, cloned_entry);
					}
				}
				pos = entry_end;
				if(col_count == 0) {
					--row_count;
				} else {
					if(column_count == -1) column_count = col_count;
					else assert(column_count == col_count);
				}
			}
		}
		mfree(mem);
	}

	// Write macros for all tables
	createFile(tprint("%s/autogenerated_tables.h", src_dir));
	println("#ifndef AUTOGENERATED_TABLES_INCLUDED");
	println("#define AUTOGENERATED_TABLES_INCLUDED");
	for(usize i = 0; i < tables.len; ++i){
		Table t = tables.items[i];
		const char* table_size_macro = allocPrintZ(gpa, "%s_ROW_COUNT", t.name);
		println("#define %s %d", table_size_macro, t.columns.items[0].rows.len);
		for(usize j = 0; j < t.columns.len; ++j){
			Column col = t.columns.items[j];
			if(col.emit_x_macro) {
				println("#define %s_X_%s \\", t.name, col.name);
				for(usize k = 0; k < col.rows.len; ++k){
					if(strcmp(col.rows.items[k], "-") == 0) continue;
					println("X_MACRO(%s) \\", col.rows.items[k]);
				}
				println("");
			}
			if(col.is_hidden) continue;
			if(col.kind != Array && !col.is_internal) continue;
			usize row_count;
			const char* size_macro = getSizeMacro(t, col, table_size_macro, &row_count);
			println("#define %s_%s \\", t.name, t.columns.items[j].name);
			if(!col.is_internal) {
				println("(%s[%s]) { \\", col.type, size_macro); ++indent;
			}
			printColumnInternals(col, row_count);
			if(!col.is_internal) {
				--indent; println("}");
			} else println("");
		}
		println("#define %s%s(...) \\", prefix, t.name);
		for(usize j = 0; j < t.columns.len; ++j){
			Column col = t.columns.items[j];
			if(strcmp(col.type, "-") == 0) continue;
			if(col.kind == Array || col.is_internal) continue;
			println("typedef %s { \\", col.type); ++indent;
			usize row_count;
			getSizeMacro(t, col, table_size_macro, &row_count);
			printColumnInternals(col, row_count);
			--indent; println("} %s; \\", col.name);
		}
		println("");
	}
	println("#endif // AUTOGENERATED_TABLES_INCLUDED");
	deinitArena(gpa);
	closeFile();
}


int main(int argc, char** argv) {
	if(argc < 2) {
		printf("Source directory not provided\n");
		return 2;
	}

	genTables(argv[1]);
	return 0;
}
