/******************************************************************************
 * @file            write.c
 *****************************************************************************/
#include    <stdlib.h>
#include    <string.h>

#include    "as.h"
#include    "fixup.h"
#include    "frag.h"
#include    "intel.h"
#include    "lib.h"
#include    "report.h"
#include    "section.h"
#include    "symbol.h"
#include    "write.h"

static unsigned long relax_align (unsigned long address, unsigned long alignment) {

    unsigned long mask, new_address;
    
    mask = ~(~((unsigned int) 0) << alignment);
    new_address = (address + mask) & ~mask;
    
    return new_address - address;

}

static void relax_section (section_t section) {

    struct frag *root_frag, *frag;
    unsigned long address, frag_count, max_iterations;
    unsigned long alignment_needed;
    
    long change;
    int changed;
    
    section_set (section);
    
    root_frag = current_frag_chain->first_frag;
    address   = 0;
    
    for (frag_count = 0, frag = root_frag; frag; frag_count++, frag = frag->next) {
    
        frag->relax_marker  = 0;
        frag->address       = address;
        
        address += frag->fixed_size;
        
        switch (frag->relax_type) {
        
            case RELAX_TYPE_NONE_NEEDED:
            
                break;
            
            case RELAX_TYPE_ALIGN:
            case RELAX_TYPE_ALIGN_CODE:
            
                alignment_needed = relax_align (address, frag->offset);

                if (frag->relax_subtype != 0 && alignment_needed > frag->relax_subtype) {
                    alignment_needed = 0;
                }

                address += alignment_needed;
                break;
            
            case RELAX_TYPE_CALL: {
            
                if (frag->symbol) {
                
                    unsigned long old_frag_fixed_size = frag->fixed_size;
                    long amount = symbol_get_value (frag->symbol);
                    
                    if (symbol_is_undefined (frag->symbol)) {
                        frag->fixed_size += 4;
                    } else if (amount < 32767) {
                        frag->fixed_size += 2;
                    } else {
                        frag->fixed_size += 4;
                    }
                    
                    address += (frag->fixed_size - old_frag_fixed_size);
                
                }
                
                break;
            
            }
            
            case RELAX_TYPE_ORG:
            case RELAX_TYPE_SPACE:
            
                break;
            
            case RELAX_TYPE_MACHINE_DEPENDENT:
            
                address += machine_dependent_estimate_size_before_relax (frag, section);
                break;
            
            default:
            
                report_at (__FILE__, __LINE__, REPORT_INTERNAL_ERROR, "invalid relax type");
                exit (EXIT_FAILURE);
        
        }
    
    }
    
    /**
     * Prevents an infinite loop caused by frag growing because of a symbol that moves when the frag grows.
     *
     * Example:
     *
     *      .org _abc + 2
     *      _abc:
     */
    max_iterations = frag_count * frag_count;
    
    /* Too many frags might cause an overflow. */
    if (max_iterations < frag_count) {
        max_iterations = frag_count;
    }
    
    do {
    
        change = 0;
        changed = 0;
        
        for (frag = root_frag; frag; frag = frag->next) {
        
            long growth = 0;
            unsigned long old_address;
            
            unsigned long old_offset;
            unsigned long new_offset;
            
            frag->relax_marker = !frag->relax_marker;
            
            old_address = frag->address;
            frag->address += change;
            
            switch (frag->relax_type) {
            
                case RELAX_TYPE_NONE_NEEDED:
                
                    growth = 0;
                    break;
                
                case RELAX_TYPE_ALIGN:
                case RELAX_TYPE_ALIGN_CODE:
                
                    old_offset = relax_align (old_address + frag->fixed_size, frag->offset);
                    new_offset = relax_align (frag->address + frag->fixed_size, frag->offset);
                    
                    if (frag->relax_subtype != 0) {

                        if (old_offset > frag->relax_subtype) {
                            old_offset = 0;
                        }
                        if (new_offset > frag->relax_subtype) {
                            new_offset = 0;
                        }
                    }

                        
                    growth = new_offset - old_offset;
                    break;
                
                case RELAX_TYPE_CALL: {
                
                    growth = 0;
                    
                    if (frag->symbol) {
                    
                        long amount = symbol_get_value (frag->symbol);
                        int size = 0;
                        
                        if (symbol_is_undefined (frag->symbol)) {
                            size = 4;
                        } else if (amount < 32767) {
                        
                            size = 2;
                            frag->symbol = NULL;
                        
                        } else {
                        
                            size = 4;
                            frag->symbol = NULL;
                        
                        }
                        
                        fixup_new (frag, frag->opcode_offset_in_buf, size, frag->symbol, amount, 0, RELOC_TYPE_DEFAULT);
                    
                    } else {
                        /*fixup_new (frag, frag->opcode_offset_in_buf, 4, frag->symbol, frag->offset, 0, RELOC_TYPE_DEFAULT);*/
                    }
                    
                    break;
                
                }
                
                case RELAX_TYPE_ORG: {
                
                    unsigned long target = frag->offset;
                    
                    if (frag->symbol) {
                        target += symbol_get_value (frag->symbol);
                    }
                    
                    growth = target - (frag->next->address + change);
                    
                    if (frag->address + frag->fixed_size > target) {
                    
                        report_at (frag->filename, frag->line_number, REPORT_ERROR, "attempt to move .org backwards");
                        growth = 0;
                        
                        /* Changes the frag so no more errors appear because of it. */
                        frag->relax_type = RELAX_TYPE_ALIGN;
                        frag->offset = 0;
                        frag->fixed_size = frag->next->address + change - frag->address;
                    
                    }
                    
                    break;
                
                }
                
                case RELAX_TYPE_SPACE:
                
                    growth = 0;
                    
                    if (frag->symbol) {
                    
                        long amount = symbol_get_value (frag->symbol);
                        
                        if (symbol_get_section (frag->symbol) != absolute_section || symbol_is_undefined (frag->symbol)) {
                        
                            report_at (frag->filename, frag->line_number, REPORT_ERROR, ".space specifies non-absolute value");
                            
                            /* Prevents the error from repeating. */
                            frag->symbol = NULL;
                        
                        } else if (amount < 0) {
                        
                            report_at (frag->filename, frag->line_number, REPORT_WARNING, ".space with negative value, ignoring");
                            frag->symbol = NULL;
                        
                        } else {
                            growth = old_address + frag->fixed_size + amount - frag->next->address;
                        }
                    
                    }
                    
                    break;
                
                case RELAX_TYPE_MACHINE_DEPENDENT:
                
                    growth = machine_dependent_relax_frag (frag, section, change);
                    break;
                
                default:
                
                    report_at (__FILE__, __LINE__, REPORT_INTERNAL_ERROR, "invalid relax type");
                    exit (EXIT_FAILURE);
            
            }
            
            if (growth) {
            
                change += growth;
                changed = 1;
            
            }
        
        }
    
    } while (changed && --max_iterations);
    
    if (changed) {
    
        report_at (NULL, 0, REPORT_FATAL_ERROR, "Infinite loop encountered whilst attempting to compute the addresses in section %s", section_get_name (section));
        exit (EXIT_FAILURE);
    
    }

}

static void finish_frags_after_relaxation (section_t section) {

    struct frag *root_frag, *frag;
    
    section_set (section);
    root_frag = current_frag_chain->first_frag;
    
    for (frag = root_frag; frag; frag = frag->next) {
    
        switch (frag->relax_type) {
        
            case RELAX_TYPE_NONE_NEEDED:
            
                break;
            
            case RELAX_TYPE_ALIGN:
            case RELAX_TYPE_ALIGN_CODE:
            case RELAX_TYPE_ORG:
            case RELAX_TYPE_SPACE: {
            
                unsigned char fill, *p;
                offset_t i;
                
                frag->offset = frag->next->address - (frag->address + frag->fixed_size);
                
                if (((long) (frag->offset)) < 0) {
                
                    report_at (frag->filename, frag->line_number, REPORT_ERROR, "attempt to .org/.space backward (%li)", frag->offset);
                    frag->offset = 0;
                
                }
                
                p = finished_frag_increase_fixed_size_by_frag_offset (frag);
                fill = *p;
                
                for (i = 0; i < frag->offset; i++) {
                    p[i] = fill;
                }
                
                break;
            
            }
            
            case RELAX_TYPE_CALL: {
            
                unsigned char fill = 0, *p;
                offset_t i;
                
                frag->offset = frag->next->address - (frag->address + frag->fixed_size);
                
                if (((long) (frag->offset)) < 0) {
                    frag->offset = 0;
                }
                
                p = finished_frag_increase_fixed_size_by_frag_offset (frag);
                
                for (i = 0; i < frag->offset; i++) {
                    p[i] = fill;
                }
                
                break;
            
            }
            
            case RELAX_TYPE_MACHINE_DEPENDENT:
            
                machine_dependent_finish_frag (frag);
                break;
            
            default:
            
                report_at (__FILE__, __LINE__, REPORT_INTERNAL_ERROR, "invalid relax type");
                exit (EXIT_FAILURE);
        
        }
    
    }

}

static void adjust_reloc_symbols_of_section (section_t section) {

    struct fixup *fixup;
    section_set (section);
    
    for (fixup = current_frag_chain->first_fixup; fixup; fixup = fixup->next) {
    
        if (fixup->done) { continue; }
        
        if (fixup->add_symbol) {
        
            struct symbol *symbol = fixup->add_symbol;
            
            /* Resolves symbols that have not been resolved yet (expression symbols). */
            symbol_resolve_value (symbol);
            
            if (symbol_uses_reloc_symbol (symbol)) {
            
                fixup->add_number += symbol_get_value_expression (symbol)->add_number;
                symbol = symbol_get_value_expression (symbol)->add_symbol;
                fixup->add_symbol = symbol;
            
            }
            
            if (symbol_force_reloc (symbol)) {
                continue;
            }
            
            if (symbol_get_section (symbol) == absolute_section) {
                continue;
            }
            
            fixup->add_number += symbol_get_value (symbol);
            fixup->add_symbol = section_symbol (symbol_get_section (symbol));
        
        }
    
    }

}

static unsigned long fixup_section (section_t section) {

    struct fixup *fixup;
    section_t add_symbol_section;
    
    unsigned long add_number, section_reloc_count = 0;
    unsigned char *p;
    
    section_set (section);
    
    for (fixup = current_frag_chain->first_fixup; fixup; fixup = fixup->next) {
    
        add_number = fixup->add_number;
        
        if (fixup->add_symbol) {
        
            add_symbol_section = symbol_get_section (fixup->add_symbol);
            
            if ((add_symbol_section == section) && !machine_dependent_force_relocation_local (fixup)) {
            
                add_number += symbol_get_value (fixup->add_symbol);
                fixup->add_number = add_number;
                
                if (fixup->pcrel) {
                
                    add_number -= machine_dependent_pcrel_from (fixup);
                    fixup->pcrel = 0;
                
                }
                
                fixup->add_symbol = NULL;
            
            } else if (add_symbol_section == absolute_section) {
            
                add_number += symbol_get_value (fixup->add_symbol);
                
                fixup->add_number = add_number;
                fixup->add_symbol = NULL;
            
            }
        
        }
        
        p = fixup->frag->buf + fixup->where;
        
        if (*(p - 1) == 0x9A) {
        
            if (state->seg_jmp && fixup->add_symbol == NULL) {
            
                add_number -= (fixup->size + fixup->where + fixup->frag->address);
                
                if ((long) add_number < 32767) {
                
                    machine_dependent_number_to_chars (p, add_number, 2);
                    *(p - 1) = 0xE8;
                
                } else {
                
                    unsigned short segment = add_number / 16;
                    unsigned short offset = add_number % 16;
                    
                    machine_dependent_number_to_chars (p, offset, 2);
                    machine_dependent_number_to_chars (p, segment, 2);
                
                }
            
            }
        
        }
        
        if (fixup->pcrel) {
            add_number -= machine_dependent_pcrel_from (fixup);
        }
        
        machine_dependent_apply_fixup (fixup, add_number);
        
        if (fixup->done == 0) {
            section_reloc_count++;
        }
    
    }
    
    return section_reloc_count;

}

void write_object_file (struct object_format *obj_fmt) {

    struct symbol *symbol;
    section_t section;
    
    sections_chain_subsection_frags ();
    
    for (section = sections; section; section = section_get_next_section (section)) {
        relax_section (section);
    }
    
    for (section = sections; section; section = section_get_next_section (section)) {
        finish_frags_after_relaxation (section);
    }
    
    if (obj_fmt->adjust_code) {
        (obj_fmt->adjust_code) ();
    }
    
    finalize_symbols = 1;

    for (symbol = symbols; symbol; symbol = symbol->next) {
        symbol_resolve_value (symbol);
    }
    
    for (section = sections; section; section = section_get_next_section (section)) {
        adjust_reloc_symbols_of_section (section);
    }
    
    for (section = sections; section; section = section_get_next_section (section)) {
        fixup_section (section);
    }
    
    if (obj_fmt->write_object) {
        (obj_fmt->write_object) ();
    }

}
