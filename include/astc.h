#pragma once

#include "g.h"

/*
 * Initial C AST data model for parsing headers and generating C.
 * Nodes and list storage are owned by the caller (for example, an arena).
 * All pointers are borrowed; this header performs no allocation or cleanup.
 * Text is a byte slice, not necessarily null-terminated. Its backing storage
 * must outlive the AST. Empty lists may have a null items pointer.
 * Zero-initialized nodes have an invalid kind until explicitly initialized.
 * Preprocessor state and compiler-specific extensions are outside this model.
 */

typedef struct astc_text {
    const char *data;
    usize len;
} astc_text;

/* Half-open byte offsets in file; an empty file identifies generated nodes. */
typedef struct astc_span {
    astc_text file;
    usize begin;
    usize end;
} astc_span;

typedef struct astc_type astc_type;
typedef struct astc_expr astc_expr;
typedef struct astc_stmt astc_stmt;
typedef struct astc_decl astc_decl;
typedef struct astc_initializer astc_initializer;

typedef struct astc_type_list {
    astc_type **items;
    usize count;
} astc_type_list;

typedef struct astc_expr_list {
    astc_expr **items;
    usize count;
} astc_expr_list;

typedef struct astc_stmt_list {
    astc_stmt **items;
    usize count;
} astc_stmt_list;

typedef struct astc_decl_list {
    astc_decl **items;
    usize count;
} astc_decl_list;

typedef enum astc_type_kind {
    ASTC_TYPE_INVALID,
    ASTC_TYPE_VOID,
    ASTC_TYPE_BOOL,
    ASTC_TYPE_CHAR,
    ASTC_TYPE_SIGNED_CHAR,
    ASTC_TYPE_UNSIGNED_CHAR,
    ASTC_TYPE_SHORT,
    ASTC_TYPE_UNSIGNED_SHORT,
    ASTC_TYPE_INT,
    ASTC_TYPE_UNSIGNED_INT,
    ASTC_TYPE_LONG,
    ASTC_TYPE_UNSIGNED_LONG,
    ASTC_TYPE_LONG_LONG,
    ASTC_TYPE_UNSIGNED_LONG_LONG,
    ASTC_TYPE_FLOAT,
    ASTC_TYPE_DOUBLE,
    ASTC_TYPE_LONG_DOUBLE,
    ASTC_TYPE_POINTER,
    ASTC_TYPE_ARRAY,
    ASTC_TYPE_FUNCTION,
    ASTC_TYPE_STRUCT,
    ASTC_TYPE_UNION,
    ASTC_TYPE_ENUM,
    ASTC_TYPE_TYPEDEF
} astc_type_kind;

typedef enum astc_type_qualifier {
    ASTC_QUAL_CONST = 1u << 0,
    ASTC_QUAL_VOLATILE = 1u << 1,
    ASTC_QUAL_RESTRICT = 1u << 2,
    ASTC_QUAL_ATOMIC = 1u << 3
} astc_type_qualifier;

struct astc_type {
    astc_type_kind kind;
    u32 qualifiers; /* Bitwise OR of astc_type_qualifier values. */
    astc_span span;
    union {
        struct { astc_type *pointee; } pointer;
        struct {
            astc_type *element;
            astc_expr *bound; /* Null for an unspecified bound. */
        } array;
        struct {
            astc_type *result;
            astc_decl_list parameters; /* ASTC_DECL_PARAMETER nodes. */
            u8 has_prototype; /* Distinguishes f(void) from pre-C23 f(). */
            u8 is_variadic;
        } function;
        /* Struct, union, enum, or typedef declaration referenced by this type. */
        struct { astc_decl *declaration; } named;
    } data;
};

typedef enum astc_operator {
    ASTC_OP_INVALID,
    ASTC_OP_POSITIVE, ASTC_OP_NEGATIVE,
    ASTC_OP_BIT_NOT, ASTC_OP_LOGICAL_NOT,
    ASTC_OP_ADDRESS, ASTC_OP_DEREFERENCE,
    ASTC_OP_PRE_INCREMENT, ASTC_OP_PRE_DECREMENT,
    ASTC_OP_POST_INCREMENT, ASTC_OP_POST_DECREMENT,
    ASTC_OP_ADD, ASTC_OP_SUBTRACT, ASTC_OP_MULTIPLY,
    ASTC_OP_DIVIDE, ASTC_OP_REMAINDER,
    ASTC_OP_SHIFT_LEFT, ASTC_OP_SHIFT_RIGHT,
    ASTC_OP_LESS, ASTC_OP_LESS_EQUAL, ASTC_OP_GREATER, ASTC_OP_GREATER_EQUAL,
    ASTC_OP_EQUAL, ASTC_OP_NOT_EQUAL,
    ASTC_OP_BIT_AND, ASTC_OP_BIT_XOR, ASTC_OP_BIT_OR,
    ASTC_OP_LOGICAL_AND, ASTC_OP_LOGICAL_OR,
    ASTC_OP_ASSIGN,
    ASTC_OP_ADD_ASSIGN, ASTC_OP_SUBTRACT_ASSIGN, ASTC_OP_MULTIPLY_ASSIGN,
    ASTC_OP_DIVIDE_ASSIGN, ASTC_OP_REMAINDER_ASSIGN,
    ASTC_OP_SHIFT_LEFT_ASSIGN, ASTC_OP_SHIFT_RIGHT_ASSIGN,
    ASTC_OP_BIT_AND_ASSIGN, ASTC_OP_BIT_XOR_ASSIGN, ASTC_OP_BIT_OR_ASSIGN,
    ASTC_OP_COMMA
} astc_operator;

typedef enum astc_expr_kind {
    ASTC_EXPR_INVALID,
    ASTC_EXPR_IDENTIFIER,
    ASTC_EXPR_INTEGER,
    ASTC_EXPR_FLOAT,
    ASTC_EXPR_CHARACTER,
    ASTC_EXPR_STRING,
    ASTC_EXPR_UNARY,
    ASTC_EXPR_BINARY,
    ASTC_EXPR_CONDITIONAL,
    ASTC_EXPR_CALL,
    ASTC_EXPR_INDEX,
    ASTC_EXPR_MEMBER,
    ASTC_EXPR_CAST,
    ASTC_EXPR_SIZEOF_EXPR,
    ASTC_EXPR_SIZEOF_TYPE,
    ASTC_EXPR_ALIGNOF_TYPE,
    ASTC_EXPR_COMPOUND_LITERAL
} astc_expr_kind;

struct astc_expr {
    astc_expr_kind kind;
    astc_span span;
    astc_type *resolved_type; /* Optional semantic information. */
    union {
        astc_text identifier;
        /* Original C spelling, including prefixes, suffixes, quotes, escapes. */
        astc_text literal;
        struct { astc_operator op; astc_expr *operand; } unary;
        struct { astc_operator op; astc_expr *left; astc_expr *right; } binary;
        struct {
            astc_expr *condition;
            astc_expr *then_value;
            astc_expr *else_value;
        } conditional;
        struct { astc_expr *callee; astc_expr_list arguments; } call;
        struct { astc_expr *base; astc_expr *index; } index;
        struct {
            astc_expr *base;
            astc_text name;
            u8 through_pointer; /* Nonzero for ->; zero for . */
        } member;
        struct { astc_type *type; astc_expr *operand; } cast;
        astc_expr *sizeof_expr;
        astc_type *type_operand; /* sizeof(type) or _Alignof(type). */
        struct { astc_type *type; astc_initializer *value; } compound_literal;
    } data;
};

typedef enum astc_designator_kind {
    ASTC_DESIGNATOR_INVALID,
    ASTC_DESIGNATOR_FIELD,
    ASTC_DESIGNATOR_INDEX
} astc_designator_kind;

typedef struct astc_designator {
    astc_designator_kind kind;
    union {
        astc_text field;
        astc_expr *index;
    } data;
} astc_designator;

typedef struct astc_init_entry {
    /* Ordered chain, e.g. .field[2]; count zero means positional initializer. */
    astc_designator *designators;
    usize designator_count;
    astc_initializer *value;
} astc_init_entry;

typedef enum astc_initializer_kind {
    ASTC_INITIALIZER_INVALID,
    ASTC_INITIALIZER_EXPR,
    ASTC_INITIALIZER_LIST
} astc_initializer_kind;

struct astc_initializer {
    astc_initializer_kind kind;
    astc_span span;
    union {
        astc_expr *expression;
        struct { astc_init_entry *items; usize count; } list;
    } data;
};

typedef enum astc_storage_class {
    ASTC_STORAGE_NONE,
    ASTC_STORAGE_EXTERN,
    ASTC_STORAGE_STATIC,
    ASTC_STORAGE_AUTO,
    ASTC_STORAGE_REGISTER
} astc_storage_class;

typedef enum astc_decl_kind {
    ASTC_DECL_INVALID,
    ASTC_DECL_VARIABLE,
    ASTC_DECL_PARAMETER,
    ASTC_DECL_FIELD,
    ASTC_DECL_FUNCTION,
    ASTC_DECL_TYPEDEF,
    ASTC_DECL_STRUCT,
    ASTC_DECL_UNION,
    ASTC_DECL_ENUM,
    ASTC_DECL_ENUMERATOR
} astc_decl_kind;

struct astc_decl {
    astc_decl_kind kind;
    astc_span span;
    astc_text name; /* Empty for anonymous tags, fields, or unnamed parameters. */
    astc_type *type; /* Declared type; underlying type for a typedef. */
    astc_storage_class storage;
    u8 is_thread_local; /* Independent of extern/static storage. */
    u8 is_inline;
    u8 is_noreturn;
    union {
        astc_initializer *initializer; /* Variable; null if absent. */
        struct { astc_expr *bit_width; } field; /* Null for a non-bit-field. */
        struct { astc_stmt *body; } function; /* Null for a prototype. */
        struct {
            astc_decl_list members; /* Fields or enumerators, in source order. */
            u8 is_definition; /* Distinguishes a forward declaration. */
        } aggregate;
        astc_expr *enum_value; /* Null for an implicit enumerator value. */
    } data;
};

typedef enum astc_stmt_kind {
    ASTC_STMT_INVALID,
    ASTC_STMT_EMPTY,
    ASTC_STMT_EXPR,
    ASTC_STMT_DECL,
    ASTC_STMT_BLOCK,
    ASTC_STMT_IF,
    ASTC_STMT_SWITCH,
    ASTC_STMT_CASE,
    ASTC_STMT_DEFAULT,
    ASTC_STMT_WHILE,
    ASTC_STMT_DO_WHILE,
    ASTC_STMT_FOR,
    ASTC_STMT_RETURN,
    ASTC_STMT_BREAK,
    ASTC_STMT_CONTINUE,
    ASTC_STMT_GOTO,
    ASTC_STMT_LABEL
} astc_stmt_kind;

struct astc_stmt {
    astc_stmt_kind kind;
    astc_span span;
    union {
        astc_expr *expression; /* Expression statement, or optional return value. */
        astc_decl_list declarations;
        astc_stmt_list block; /* Declaration statements preserve source order. */
        struct {
            astc_expr *condition;
            astc_stmt *then_body;
            astc_stmt *else_body; /* Null when absent. */
        } branch;
        struct { astc_expr *value; astc_stmt *body; } switch_stmt;
        struct { astc_expr *value; astc_stmt *body; } case_stmt;
        astc_stmt *default_body;
        struct { astc_expr *condition; astc_stmt *body; } loop;
        struct {
            astc_stmt *init; /* Null, expression statement, or declarations. */
            astc_expr *condition; /* Null means no condition. */
            astc_expr *step; /* Null when absent. */
            astc_stmt *body;
        } for_stmt;
        astc_text goto_label;
        struct { astc_text name; astc_stmt *body; } label;
    } data;
};

typedef struct astc_translation_unit {
    astc_text file;
    astc_decl_list declarations; /* Top-level declarations in source order. */
} astc_translation_unit;
