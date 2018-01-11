#ifndef _SYMTAB_VISITOR_HELPER_HPP_
#define _SYMTAB_VISITOR_HELPER_HPP_

#include "lexer/token_mapping.hpp"
#include "symtab/symbol_table.hpp"

/// helper function to setup/insert symbol into symbol table
/// for the ast nodes which are of variable types
template <typename T>
void SymtabVisitor::setup_symbol(T* node, SymbolInfo property, int order) {
    std::shared_ptr<Symbol> symbol;
    auto token = node->get_token();

    /// if prime variable is already exist in symbol table
    /// then just update the order
    if (order) {
        symbol = modsymtab->lookup(node->get_name());
        if (symbol) {
            symbol->set_order(order);
            symbol->add_property(property);
            return;
        }
    }

    /// add new symbol
    symbol = std::make_shared<Symbol>(node->get_name(), node, *token);
    symbol->add_property(property);
    modsymtab->insert(symbol);

    /// visit childrens, most likely variables are already
    /// leaf nodes, not necessary to visit
    node->visit_children(this);
}

template <typename T>
void SymtabVisitor::setup_symbol_table(T* node,
                                       std::string name,
                                       SymbolInfo property,
                                       bool is_global) {
    auto token = node->get_token();
    auto symbol = std::make_shared<Symbol>(name, node, *token);
    symbol->add_property(property);
    modsymtab->insert(symbol);
    setup_symbol_table(node, name, is_global);
}

template <typename T>
void SymtabVisitor::setup_symbol_table(T* node, std::string name, bool is_global) {
    /// entering into new nmodl block
    auto symtab = modsymtab->enter_scope(name, node, is_global);

    /// not required at the moment but every node
    /// has pointer to associated symbol table
    node->set_symbol_table(symtab);

    /// when visiting highest level node i.e. Program, we insert
    /// all global variables to the global symbol table
    if (node->is_program()) {
        ModToken tok(true);
        auto variables = nmodl::get_external_variables();
        for (auto variable : variables) {
            auto symbol = std::make_shared<Symbol>(variable, nullptr, tok);
            symbol->add_property(symtab::details::NmodlInfo::extern_neuron_variable);
            modsymtab->insert(symbol);
        }
        auto methods = nmodl::get_external_functions();
        for (auto method : methods) {
            auto symbol = std::make_shared<Symbol>(method, nullptr, tok);
            symbol->add_property(symtab::details::NmodlInfo::extern_method);
            modsymtab->insert(symbol);
        }
    }

    /// look for all children blocks recursively
    node->visit_children(this);

    /// exisiting nmodl block
    modsymtab->leave_scope();
}

#endif