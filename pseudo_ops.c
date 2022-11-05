/******************************************************************************
 * @file            pseudo_ops.c
 *****************************************************************************/
#include    <stddef.h>
#include    <stdlib.h>

#include    "expr.h"
#include    "fixup.h"
#include    "frag.h"
#include    "hashtab.h"
#include    "lib.h"
#include    "pseudo_ops.h"
#include    "report.h"
#include    "section.h"
#include    "symbol.h"
#include    "types.h"

extern char get_symbol_name_end (char **pp);
extern int read_and_append_char_in_ascii (char **pp, int size);

extern void demand_empty_rest_of_line (char **pp);
extern void ignore_rest_of_line (char **pp);

static void do_org (section_t section, struct expr *expr, unsigned long fill_value) {

    struct symbol *symbol;
    
    unsigned long offset;
    unsigned char *p_in_frag;
    
    if (section != current_section && section != absolute_section && section != expr_section) {
        report (REPORT_ERROR, "invalid section \"%s\"", section_get_name (section));
    }
    
    symbol = expr->add_symbol;
    offset = expr->add_number;
    
    if (fill_value && current_section == bss_section) {
        report (REPORT_WARNING, "ignoring fill value in section \"%s\"", section_get_name (current_section));
    }
    
    if (expr->type != EXPR_TYPE_CONSTANT && expr->type != EXPR_TYPE_SYMBOL) {
    
        symbol = make_expr_symbol (expr);
        offset = 0;
    
    }
    
    p_in_frag = frag_alloc_space (1);
    *p_in_frag = (unsigned char) fill_value;
    
    frag_set_as_variant (RELAX_TYPE_ORG, 0, symbol, offset, 0);

}

static section_t get_known_section_expression (char **pp, struct expr *expr) {

    section_t section = expression_read_into (pp, expr);
    
    if (expr->type == EXPR_TYPE_INVALID || expr->type == EXPR_TYPE_ABSENT) {
    
        report (REPORT_ERROR, "expected address expression");
        
        expr->type = EXPR_TYPE_CONSTANT;
        expr->add_number = 0;
        
        section = absolute_section;
    
    }
    
    if (section == undefined_section) {
    
        if (expr->add_symbol && symbol_get_section (expr->add_symbol) != expr_section) {
            report (REPORT_WARNING, "symbol \"%s\" undefined; zero assumed", symbol_get_name (expr->add_symbol));
        } else {
            report (REPORT_WARNING, "some symbol undefined; zero assumed");
        }
        
        expr->type = EXPR_TYPE_CONSTANT;
        expr->add_number = 0;
        
        section = absolute_section;
    
    }
    
    return section;

}

static void internal_set (char **pp, struct symbol *symbol) {

    struct expr expr;
    
    expression_read_into (pp, &expr);
    
    if (expr.type == EXPR_TYPE_INVALID) {
        report (REPORT_ERROR, "invalid expression");
    } else if (expr.type == EXPR_TYPE_ABSENT) {
        report (REPORT_ERROR, "missing expression");
    }
    
    if (symbol_is_section_symbol (symbol)) {
    
        report (REPORT_ERROR, "attempt to set value of section symbol");
        return;
    
    }
    
    switch (expr.type) {
    
        case EXPR_TYPE_INVALID:
        case EXPR_TYPE_ABSENT:
        
            expr.add_number = 0;
            /* fall through. */
        
        case EXPR_TYPE_CONSTANT:
        
            symbol_set_frag (symbol, &zero_address_frag);
            symbol_set_section (symbol, absolute_section);
            symbol_set_value (symbol, expr.add_number);
            break;
        
        default:
        
            symbol_set_frag (symbol, &zero_address_frag);
            symbol_set_section (symbol, expr_section);
            symbol_set_value_expression (symbol, &expr);
            break;
    
    }

}


static void handler_align (char **pp, int first_arg_is_bytes) {

    offset_t alignment, fill_value = 0, i, max_bytes_to_skip;
    int fill_specified;
    
    alignment = get_result_of_absolute_expression (pp);
    
    if (first_arg_is_bytes) {
    
        /* Converts to log2. */    
        for (i = 0; (alignment & 1) == 0; alignment >>= 1, i++);
        
        if (alignment != 1) {
            report (REPORT_ERROR, "alignment is not a power of 2!");
        }
        
        alignment = i;
    
    }
    
    if (**pp != ',') {
    
        fill_specified = 0;
        max_bytes_to_skip = 0;
    
    } else {
    
        ++*pp;
        *pp = skip_whitespace (*pp);
        
        if (**pp == ',') {
            fill_specified = 0;
        } else {
        
            fill_value = get_result_of_absolute_expression (pp);
            fill_specified = 1;
        
        }
        
        if (**pp != ',') {
            max_bytes_to_skip = 0;
        } else {
        
            ++*pp;
            max_bytes_to_skip = get_result_of_absolute_expression (pp);
        
        }
    
    }
    
    if (fill_specified) {
        frag_align (alignment, fill_value, max_bytes_to_skip);
    } else {
    
        if (current_section == text_section) {
            frag_align_code (alignment, max_bytes_to_skip);
        } else {
            frag_align (alignment, 0, max_bytes_to_skip);
        }
    
    }
    
    demand_empty_rest_of_line (pp);

}

static void handler_constant (char **pp, int size, int is_rva) {

    struct expr expr, val;
    offset_t repeat;
    
    char saved_ch, *tmp;
    int i;
    
    do {
    
        tmp = (*pp = skip_whitespace (*pp));
        
        if (current_section == bss_section) {
        
            report (REPORT_WARNING, "attempt to initialize memory in a nobits section: ignored");
            ignore_rest_of_line (pp);
            
            continue;
        
        }
        
        if (**pp == '"') {
        
            ++(*pp);
            
            while (read_and_append_char_in_ascii (pp, size) == 0) {
                /* Nothing to do */
            }
        
        }
        
        expression_read_into (pp, &expr);
        
        tmp = (*pp = skip_whitespace (*pp));
        saved_ch = get_symbol_name_end (pp);
        
        if (xstrcasecmp (tmp, "dup") == 0) {
        
            **pp = saved_ch;
            
            *pp = skip_whitespace (*pp);
            expression_read_into (pp, &val);
            
            if (val.type != EXPR_TYPE_CONSTANT) {
            
                report (REPORT_ERROR, "invalid value for dup");
                goto skip;
            
            }
            
            if (expr.type == EXPR_TYPE_CONSTANT) {
            
                repeat = expr.add_number;
                
                if (repeat == 0) {
                
                    report (REPORT_WARNING, "dup repeat count is zero, ignored");
                    goto skip;
                
                }
                
                if (repeat < 0) {
                
                    report (REPORT_WARNING, "dup repeat count is negative, ignored");
                    goto skip;
                
                }
                
                while (repeat--) {
                
                    for (i = 0; i < size; ++i) {
                        frag_append_1_char ((val.add_number >> (8 * i)) & 0xff);
                    }
                
                }
            
            } else {
            
                struct symbol *expr_symbol = make_expr_symbol (&expr);
                
                unsigned char *p = frag_alloc_space (symbol_get_value (expr_symbol));
                *p = val.add_number;
                
                frag_set_as_variant (RELAX_TYPE_SPACE, 0, expr_symbol, 0, 0);
            
            }
            
        skip:
            
            *pp = skip_whitespace (*pp);
            continue;
        
        }
        
        **pp = saved_ch;
        
        if (is_rva) {
        
            if (expr.type == EXPR_TYPE_SYMBOL) {
                expr.type = EXPR_TYPE_SYMBOL_RVA;
            } else {
                report (REPORT_ERROR, "rva without symbol.");
            }
        
        }
        
        if (expr.type == EXPR_TYPE_CONSTANT) {
        
            int i;
            
            for (i = 0; i < size; i++) {
                frag_append_1_char ((expr.add_number >> (8 * i)) & 0xff);
            }
        
        } else if (expr.type != EXPR_TYPE_INVALID) {
        
            fixup_new_expr (current_frag, current_frag->fixed_size, size, &expr, 0, RELOC_TYPE_DEFAULT);
            frag_increase_fixed_size (size);
        
        } else {
            report (REPORT_ERROR, "value is not a constant");
        }
        
        *pp = skip_whitespace (*pp);
    
    } while (*((*pp)++) == ',');
    
    demand_empty_rest_of_line (pp);

}

static void handler_reserve (char **pp, int size) {

    offset_t repeat;
    int i;
    
    struct expr expr;
    expression_read_into (pp, &expr);
    
    if (expr.type != EXPR_TYPE_CONSTANT) {
    
        report (REPORT_ERROR, "attempt to reserve non-constant quantity");
        ignore_rest_of_line (pp);
    
    } else {
    
        repeat = expr.add_number;
        
        if (repeat == 0) {
        
            report (REPORT_WARNING, "trying to reserve zero %s, ignored", (size == 1 ? "bytes" : (size == 2 ? "words" : "dwords")));
            return;
        
        }
        
        if (repeat < 0) {
        
            report (REPORT_WARNING, "trying to reserve negative %s, ignored", (size == 1 ? "bytes" : (size == 2 ? "words" : "dwords")));
            return;
        
        }
        
        while (repeat--) {
        
            for (i = 0; i < size; i++) {
                frag_append_1_char (0);
            }
        
        }
        
        demand_empty_rest_of_line (pp);
    
    }

}


static void handler_align_bytes (char **pp) {
    handler_align (pp, 1);
}

static void handler_ascii (char **pp) {

    char ch;
    
    if ((ch = *((*pp)++)) == '"') {
    
        while (!read_and_append_char_in_ascii (pp, 1)) {
            /* Nothing to do */
        }
    
    }
    
    demand_empty_rest_of_line (pp);

}

static void handler_asciz (char **pp) {

    handler_ascii (pp);
    frag_append_1_char (0);

}

static void handler_byte (char **pp) {
    handler_constant (pp, 1, 0);
}

static void handler_global (char **pp) {
    
    for (;;) {
    
        struct symbol *symbol;
        
        char *name;
        char ch;
        
        *pp = skip_whitespace (*pp);
        name = *pp;
        
        ch = get_symbol_name_end (pp);
        
        if (name == *pp) {
        
            report (REPORT_ERROR, "expected symbol name");
            
            **pp = ch;
            ignore_rest_of_line (pp);
            
            return;
        
        }
        
        symbol = symbol_find_or_make (name);
        symbol_set_external (symbol);
        
        **pp = ch;
        *pp = skip_whitespace (*pp);
        
        if (**pp != ',') {
            break;
        }
        
        ++*pp;
    
    }
    
    demand_empty_rest_of_line (pp);

}

static void handler_long (char **pp) {
    handler_constant (pp, 4, 0);
}

static void handler_org (char **pp) {

    struct expr expr;
    unsigned long fill_value;
    
    section_t section = get_known_section_expression (pp, &expr);
    
    if (**pp == ',') {
    
        ++(*pp);
        
        report_at (__FILE__, __LINE__, REPORT_INTERNAL_ERROR, "+++handler_org");
        fill_value = 0;
    
    } else {
        fill_value = 0;
    }
    
    do_org (section, &expr, fill_value);
    demand_empty_rest_of_line (pp);

}

static void handler_resb (char **pp) {
    handler_reserve (pp, 1);
}

static void handler_resd (char **pp) {
    handler_reserve (pp, 1);
}

static void handler_resw (char **pp) {
    handler_reserve (pp, 1);
}

static void handler_space (char **pp) {

    struct expr expr, val;
    offset_t repeat;
    
    expression_read_into (pp, &expr);
    
    if (**pp == ',') {
    
        ++(*pp);
        expression_read_into (pp, &val);
        
        if (val.type != EXPR_TYPE_CONSTANT) {
        
            report (REPORT_ERROR, "invalid value for .space");
            
            ignore_rest_of_line (pp);
            return;
        
        }
    
    } else {
    
        val.type = EXPR_TYPE_CONSTANT;
        val.add_number = 0;
    
    }
    
    if (expr.type == EXPR_TYPE_CONSTANT) {
    
        repeat = expr.add_number;
        
        if (repeat == 0) {
        
            report (REPORT_WARNING, ".space repeat count is zero, ignored");
            goto end;
        
        }
        
        if (repeat < 0) {
        
            report (REPORT_WARNING, ".space repeat count is negative, ignored");
            goto end;
        
        }
        
        while (repeat--) {
            frag_append_1_char (val.add_number);
        }
    
    } else {
    
        struct symbol *expr_symbol = make_expr_symbol (&expr);
        
        unsigned char *p = frag_alloc_space (symbol_get_value (expr_symbol));
        *p = val.add_number;
        
        frag_set_as_variant (RELAX_TYPE_SPACE, 0, expr_symbol, 0, 0);
    
    }
    
    if ((val.type != EXPR_TYPE_CONSTANT || val.add_number != 0) && current_section == bss_section) {
        report (REPORT_WARNING, "ignoring fill value in section '%s'", section_get_name (current_section));
    }
    
end:
    
    demand_empty_rest_of_line (pp);

}

static void handler_word (char **pp) {
    handler_constant (pp, 2, 0);
}


static struct pseudo_op pseudo_ops[] = {

    { ".align",     handler_align_bytes },
    { ".ascii",     handler_ascii       },
    { ".asciz",     handler_asciz       },
    { ".byte",      handler_byte        },
    { ".global",    handler_global      },
    { ".globl",     handler_global      },
    { ".long",      handler_long        },
    { ".org",       handler_org         },
    { ".word",      handler_word        },
    { ".space",     handler_space       },
    
    { "align",      handler_align_bytes },
    { "db",         handler_byte        },
    { "dd",         handler_long        },
    { "dw",         handler_word        },
    { "global",     handler_global      },
    { "org",        handler_org         },
    { "public",     handler_global      },
    { "resb",       handler_resb        },
    { "resd",       handler_resd        },
    { "resw",       handler_resw        },
    
    { NULL,         NULL                }

};

static struct hashtab pseudo_ops_hashtab = { 0 };

struct pseudo_op *find_pseudo_op (const char *name) {

    char *p = to_lower (name);
    
    struct hashtab_name *key;
    struct pseudo_op *poe;
    
    if (p == NULL) {
        return NULL;
    }
    
    if ((key = hashtab_alloc_name (p)) == NULL) {
    
        free (p);
        return NULL;
    
    }
    
    poe = hashtab_get (&pseudo_ops_hashtab, key);
    
    free (key);
    free (p);
    
    return poe;

}

void add_pseudo_op (struct pseudo_op *poe) {

    struct hashtab_name *key;
    
    if ((key = hashtab_alloc_name (poe->name)) == NULL) {
        return;
    }
    
    if (hashtab_get (&pseudo_ops_hashtab, key) != NULL) {
    
        report_at (NULL, 0, REPORT_ERROR, "pseudo op %s already exists", poe->name);
        free (key);
    
    } else if (hashtab_put (&pseudo_ops_hashtab, key, poe) < 0) {
    
        report_at (NULL, 0, REPORT_ERROR, "failed to add pseudo op %s", poe->name);
        free (key);
    
    }

}

void handler_equ (char **pp, char *name) {

    struct symbol *symbol;
    
    /* .equ ., some_expr is an alias for .org some_expr. */
    if (name[0] == '.' && name[1] == '\0') {
    
        struct expr expr;
        section_t section = get_known_section_expression (pp, &expr);
        
        do_org (section, &expr, 0);
        return;
    
    }
    
    symbol = symbol_find_or_make (name);
    internal_set (pp, symbol);

}

void pseudo_ops_init (void) {

    struct pseudo_op *poe;
    
    for (poe = pseudo_ops; poe->name; ++poe) {
        add_pseudo_op (poe);
    }

}


struct hashtab hashtab_defines = { 0 };
extern char is_end_of_line[];

int is_defined (char *p) {

    struct hashtab_name *key;
    int exists = 0;
    
    if ((key = hashtab_alloc_name (p)) == NULL) {
        return exists;
    }
    
    exists = (hashtab_get (&hashtab_defines, key) != NULL);
    free (key);
    
    return exists;

}

void handler_define (char **pp) {

    char *name;
    char *value;
    
    struct hashtab_name *key;
    char saved_ch;
    
    *pp = skip_whitespace (*pp);
    name = *pp;
    
    while (**pp && **pp != ' ' && !is_end_of_line[(int) **pp]) {
        (*pp)++;
    }
    
    saved_ch = **pp;
    **pp = '\0';
    
    if ((key = hashtab_alloc_name (xstrdup (name))) == NULL) {
    
        **pp = saved_ch;
        
        ignore_rest_of_line (pp);
        return;
    
    }
    
    *pp = skip_whitespace (*pp + 1);
    value = *pp;
    
    while (**pp && !is_end_of_line[(int) **pp]) {
        (*pp)++;
    }
    
    saved_ch = **pp;
    **pp = '\0';
    
    if (!*value) { value = "1"; }
    hashtab_put (&hashtab_defines, key, xstrdup (value));
    
    **pp = saved_ch;

}
