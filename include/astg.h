#pragma once

#include "g.h"

/*
 * Initial G AST, following astc.h's tagged-node and pointer/count list model.
 * This is a source model, not a parser, type checker, or C lowering pass.
 * The caller owns every node, list, text buffer, and referenced C AST.
 * Text is a byte slice and need not be null-terminated. Empty lists may use null.
 * Zero-initialized nodes have an invalid kind until explicitly initialized.
 * Optional resolved fields are populated by later passes, not by parsing.
 * Representation of provisional syntax does not establish additional G grammar.
 */

typedef struct astg_text {
    const char *data;
    usize len;
} astg_text;

typedef struct astg_span {
    astg_text file; /* Empty for generated nodes. */
    usize begin;   /* Half-open byte offsets. */
    usize end;
    u32 line;      /* One-based starting line; zero when unknown/generated. */
    u32 column;    /* One-based starting byte column; zero when unknown. */
} astg_span;

typedef struct astg_type astg_type;
typedef struct astg_expr astg_expr;
typedef struct astg_stmt astg_stmt;
typedef struct astg_decl astg_decl;
typedef struct astg_scope astg_scope;
struct astc_translation_unit;

typedef struct astg_expr_list {
    astg_expr **items;
    usize count;
} astg_expr_list;

typedef struct astg_stmt_list {
    astg_stmt **items;
    usize count;
} astg_stmt_list;

typedef struct astg_decl_list {
    astg_decl **items;
    usize count;
} astg_decl_list;

/* Preserve annotation arguments without fixing their still-open grammar. */
typedef struct astg_annotation {
    astg_span span;
    astg_text name;          /* Without @, e.g. ref, c, weak, strong. */
    astg_text raw_arguments; /* Empty when absent; interpretation is deferred. */
} astg_annotation;

typedef enum astg_annotation_form {
    ASTG_ANNOTATION_INVALID,
    ASTG_ANNOTATION_SHORTHAND, /* @name; exactly one annotation. */
    ASTG_ANNOTATION_GROUP     /* @(...). */
} astg_annotation_form;

typedef struct astg_annotation_group {
    astg_annotation_form form;
    astg_span span;
    astg_text spelling; /* Complete source spelling, including @ and delimiters. */
    astg_annotation *items;
    usize count; /* May be zero while a group's contents remain unparsed. */
} astg_annotation_group;

typedef struct astg_annotation_list {
    astg_annotation_group *items;
    usize count;
} astg_annotation_list;

typedef enum astg_ownership_mode {
    ASTG_OWNERSHIP_UNSPECIFIED, /* Unannotated pointer passing implies a move. */
    ASTG_OWNERSHIP_MOVE,
    ASTG_OWNERSHIP_REF,
    ASTG_OWNERSHIP_RC,
    ASTG_OWNERSHIP_POOL
} astg_ownership_mode;

typedef enum astg_reference_strength {
    ASTG_REFERENCE_UNSPECIFIED,
    ASTG_REFERENCE_WEAK,
    ASTG_REFERENCE_STRONG
} astg_reference_strength;

typedef enum astg_scope_kind {
    ASTG_SCOPE_INVALID,
    ASTG_SCOPE_FILE,
    ASTG_SCOPE_FUNCTION,
    ASTG_SCOPE_BLOCK,
    ASTG_SCOPE_CALL /* Function calls introduce inner scopes for leases. */
} astg_scope_kind;

struct astg_scope {
    astg_scope_kind kind;
    astg_span span;
    astg_scope *parent;
};

/*
 * Optional normalized semantic data. Raw annotations remain authoritative.
 * Keep null until annotations have been interpreted; do not silently collapse
 * conflicting annotations into one mode. Combination/placement rules are pending.
 */
typedef struct astg_pointer_semantics {
    astg_ownership_mode ownership;
    astg_reference_strength strength; /* weak/strong struct-member metadata. */
    astg_scope *owner_scope;          /* Null until known. */
    astg_scope *lease_scope;          /* For ref: scope whose exit returns ownership. */
} astg_pointer_semantics;

typedef struct astg_metadata {
    astg_annotation_list annotations;
    astg_pointer_semantics *pointer_semantics;
} astg_metadata;

typedef enum astg_type_kind {
    ASTG_TYPE_INVALID,
    ASTG_TYPE_INT,
    ASTG_TYPE_U8, ASTG_TYPE_U16, ASTG_TYPE_U32, ASTG_TYPE_U64,
    ASTG_TYPE_I8, ASTG_TYPE_I16, ASTG_TYPE_I32, ASTG_TYPE_I64,
    ASTG_TYPE_USIZE, ASTG_TYPE_ISIZE,
    ASTG_TYPE_PTR,
    ASTG_TYPE_F16, ASTG_TYPE_F32, ASTG_TYPE_F64,
    ASTG_TYPE_CSTR, /* Null-terminated C string. */
    ASTG_TYPE_STR, /* Byte string with explicit len, no null terminator. */
    ASTG_TYPE_RC, ASTG_TYPE_ID, ASTG_TYPE_CLASS,
    ASTG_TYPE_NAMED,
    ASTG_TYPE_POINTER,       /* ^T */
    ASTG_TYPE_C_ARRAY,       /* []T or [`symbol]T */
    ASTG_TYPE_ARRAY,         /* [#]T, with element count */
    ASTG_TYPE_DICTIONARY,    /* [#K]V */
    ASTG_TYPE_SET,           /* [#K]() */
    ASTG_TYPE_VECTOR,        /* [#?]T */
    ASTG_TYPE_FIXED_FIFO_FILO,   /* [<const>]T or [<symbol>]T */
    ASTG_TYPE_DYNAMIC_FIFO_FILO, /* [<?>]T */
    ASTG_TYPE_FUNCTION,
    ASTG_TYPE_STRUCT
} astg_type_kind;

struct astg_type {
    astg_type_kind kind;
    astg_span span;
    astg_metadata metadata;
    union {
        struct {
            astg_text spelling; /* Qualified-name grammar remains open. */
            astg_decl *resolved_declaration;
        } named;
        struct { astg_type *pointee; } pointer;
        struct {
            astg_type *element;
            astg_text size_hint; /* Empty for []; symbol after the backtick otherwise. */
            astg_decl *resolved_size_hint;
        } c_array;
        astg_type *element; /* Array, vector, or dynamically sized FIFO/FILO. */
        struct { astg_type *key; astg_type *value; } dictionary;
        astg_type *set_element;
        struct {
            astg_type *element;
            astg_expr *capacity; /* Constant or symbol; not a C-array size hint. */
        } fixed_fifo_filo;
        struct {
            astg_decl_list parameters; /* ASTG_DECL_PARAMETER nodes, in order. */
            astg_type *result; /* Null for an absent result type; rules pending. */
        } function;
        astg_decl_list fields; /* Struct syntax remains to be specified. */
    } data;
};

typedef enum astg_string_form {
    ASTG_STRING_INVALID,
    ASTG_STRING_PLAIN,     /* "..." -- termination semantics not yet specified. */
    ASTG_STRING_NON_NULL_TERMINATED /* @"..." -- no trailing null appended. */
} astg_string_form;

typedef struct astg_string_literal {
    astg_string_form form;
    astg_text spelling; /* Includes quotes, prefix, and original escapes. */
} astg_string_literal;

typedef enum astg_expr_kind {
    ASTG_EXPR_INVALID,
    ASTG_EXPR_IDENTIFIER,
    ASTG_EXPR_INTEGER,
    ASTG_EXPR_FLOAT,
    ASTG_EXPR_STRING,
    ASTG_EXPR_UNARY,
    ASTG_EXPR_BINARY,
    ASTG_EXPR_CALL,
    ASTG_EXPR_INDEX,
    ASTG_EXPR_MEMBER,
    ASTG_EXPR_IOTA
} astg_expr_kind;

struct astg_expr {
    astg_expr_kind kind;
    astg_span span;
    astg_metadata metadata;
    astg_type *resolved_type;
    union {
        struct { astg_text spelling; astg_decl *resolved_declaration; } identifier;
        astg_text literal; /* Numeric source spelling, not a host numeric value. */
        astg_string_literal string;
        /* Operator spellings are retained; the full G operator grammar is open. */
        struct { astg_text op; astg_expr *operand; u8 is_postfix; } unary;
        struct { astg_text op; astg_expr *left; astg_expr *right; } binary;
        struct {
            astg_expr *callee;
            astg_expr_list arguments;
            astg_scope *scope; /* Optional ASTG_SCOPE_CALL for pointer leases. */
        } call;
        struct { astg_expr *base; astg_expr *index; } index;
        struct { astg_expr *base; astg_text name; } member;
        struct {
            astg_decl *group; /* Optional enclosing ASTG_DECL_IOTA_GROUP. */
            usize entry_index;
            u8 has_entry_index; /* Index is not an evaluated iota value. */
        } iota;
    } data;
};

typedef enum astg_terminator {
    ASTG_TERMINATOR_UNSPECIFIED,
    ASTG_TERMINATOR_EXPLICIT, /* Source semicolon. */
    ASTG_TERMINATOR_INSERTED  /* Automatically inserted semicolon. */
} astg_terminator;

typedef enum astg_decl_kind {
    ASTG_DECL_INVALID,
    ASTG_DECL_VARIABLE,
    ASTG_DECL_PARAMETER,
    ASTG_DECL_FIELD,
    ASTG_DECL_FUNCTION,
    ASTG_DECL_TYPE,       /* symbol type <actual type>; alias/distinct is unresolved. */
    ASTG_DECL_CONSTANT,   /* Constant/iota declaration grammar remains open. */
    ASTG_DECL_IOTA_GROUP
} astg_decl_kind;

struct astg_decl {
    astg_decl_kind kind;
    astg_span span;
    astg_metadata metadata;
    astg_terminator terminator;
    astg_text name;
    astg_type *type; /* Actual type for ASTG_DECL_TYPE; function type for functions. */
    u8 is_exported;
    /*
     * @c is retained in metadata.annotations. Lowering normally constructs
     * <library>_<name> for exports, or preserves name for @c. Do not derive the
     * library's scope here until the language's library-scope rules are defined.
     */
    astg_text resolved_library;
    astg_text generated_c_name; /* Empty until lowering chooses the name. */
    union {
        struct {
            astg_expr *value; /* Null when no initializer is present. */
            astg_text operator_spelling; /* E.g. :=; empty when absent. */
        } binding; /* Variable, field, parameter, or constant. */
        struct {
            astg_stmt *body;
            astg_scope *scope; /* Optional ASTG_SCOPE_FUNCTION. */
        } function;
        struct {
            astg_decl_list entries;
            astg_text spelling; /* Preserve syntax while iota rules remain open. */
        } iota_group;
    } data;
};

typedef struct astg_assembly {
    astg_span span;  /* Includes the %{ and } delimiters. */
    astg_text body;  /* Text inside the delimiters, in NASM-like syntax. */
} astg_assembly;

typedef enum astg_stmt_kind {
    ASTG_STMT_INVALID,
    ASTG_STMT_EMPTY,
    ASTG_STMT_EXPR,
    ASTG_STMT_DECL,
    ASTG_STMT_BLOCK,
    ASTG_STMT_RETURN,
    ASTG_STMT_ASSEMBLY
} astg_stmt_kind;

struct astg_stmt {
    astg_stmt_kind kind;
    astg_span span;
    astg_terminator terminator;
    union {
        astg_expr *expression; /* Expression statement or optional return value. */
        astg_decl_list declarations;
        struct { astg_stmt_list statements; astg_scope *scope; } block;
        astg_assembly assembly;
    } data;
};

typedef struct astg_build_filter {
    astg_span span;
    astg_text name; /* E.g. windows, arm64, darwin, amd64; excludes leading !. */
    u8 is_negated;
} astg_build_filter;

typedef enum astg_include_form {
    ASTG_INCLUDE_INVALID,
    ASTG_INCLUDE_ANGLE,
    ASTG_INCLUDE_QUOTED
} astg_include_form;

typedef enum astg_directive_kind {
    ASTG_DIRECTIVE_INVALID,
    ASTG_DIRECTIVE_BUILD,
    ASTG_DIRECTIVE_INCLUDE,
    ASTG_DIRECTIVE_USE,
    ASTG_DIRECTIVE_LIB
} astg_directive_kind;

typedef struct astg_directive {
    astg_directive_kind kind;
    astg_span span;
    union {
        struct {
            /* All filters must match the target (AND); ! negates one filter. */
            astg_build_filter *items;
            usize count;
        } build;
        struct {
            astg_include_form form;
            astg_text header; /* Without quotes or angle brackets. */
            /* Optional result of parsing the C header, not preprocessor text. */
            struct astc_translation_unit *parsed_header;
        } include;
        astg_text library; /* #use or #lib name. */
    } data;
} astg_directive;

typedef enum astg_item_kind {
    ASTG_ITEM_INVALID,
    ASTG_ITEM_DIRECTIVE,
    ASTG_ITEM_DECLARATION,
    ASTG_ITEM_ASSEMBLY /* Placement rules for assembly remain open. */
} astg_item_kind;

typedef struct astg_item {
    astg_item_kind kind;
    union {
        astg_directive *directive;
        astg_decl *declaration;
        astg_assembly *assembly;
    } data;
} astg_item;

typedef struct astg_translation_unit {
    astg_text file;
    astg_text source; /* Original file, retained for syntax and placement checks. */
    astg_item *items; /* Directives and declarations in source order. */
    usize count;
    astg_scope *scope; /* Optional ASTG_SCOPE_FILE. */
    /*
     * A #build directive, when present, must occupy the first source line,
     * before even comments or blank lines. Its span.line and source retain
     * enough information for the parser/validator to enforce this; this data
     * model alone does not validate placement or evaluate target filters.
     */
} astg_translation_unit;
