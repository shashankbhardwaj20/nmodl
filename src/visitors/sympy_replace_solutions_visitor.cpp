/*************************************************************************
 * Copyright (C) 2018-2019 Blue Brain Project
 *
 * This file is part of NMODL distributed under the terms of the GNU
 * Lesser General Public License. See top-level LICENSE file for details.
 *************************************************************************/

#include "visitors/sympy_replace_solutions_visitor.hpp"
#include "visitors/lookup_visitor.hpp"


#include "ast/all.hpp"
#include "utils/logger.hpp"
#include "visitors/visitor_utils.hpp"

namespace nmodl {
namespace visitor {


void SympyReplaceSolutionsVisitor::InterleavesCounter::new_equation(const bool is_in_system) {
    n_ += (!in_system_ && is_in_system);
    in_system_ = is_in_system;
}

SympyReplaceSolutionsVisitor::SympyReplaceSolutionsVisitor(
    const std::vector<std::string>& pre_solve_statements,
    const std::vector<std::string>& solutions,
    const std::unordered_set<ast::Statement*>& to_be_removed,
    const ReplacePolicy policy,
    const size_t n_next_equations)
    : pre_solve_statements_(pre_solve_statements.begin(), pre_solve_statements.end())
    , to_be_removed_(&to_be_removed)
    , policy_(policy)
    , n_next_equations_(n_next_equations) {
    const auto ss_tmp_delimeter =
        std::find_if(solutions.begin(), solutions.end(), [](const std::string& statement) {
            return statement.substr(0, 3) != "tmp";
        });
    tmp_statements_ = SolutionSorter(solutions.begin(), ss_tmp_delimeter);
    solutions_ = SolutionSorter(ss_tmp_delimeter, solutions.end());

    replacements_.clear();
    is_statement_block_root_ = true;
}


void SympyReplaceSolutionsVisitor::visit_statement_block(ast::StatementBlock& node) {
    const bool is_root = is_statement_block_root_;
    is_statement_block_root_ = false;

    if (is_root) {
        logger->debug("SympyReplaceSolutionsVisitor :: visit matching statements. Policy: {}",
                      (policy_ == ReplacePolicy::VALUE ? "VALUE" : "GREEDY"));
        interleaves_counter_ = InterleavesCounter();
        node.visit_children(*this);

        if (!solutions_.is_all_untagged() && policy_ == ReplacePolicy::VALUE) {
            logger->debug(
                "SympyReplaceSolutionsVisitor :: not all solutions were replaced. Policy: GREEDY");
            interleaves_counter_ = InterleavesCounter();
            policy_ = ReplacePolicy::GREEDY;
            node.visit_children(*this);

            if (interleaves_counter_.n() > 0) {
                logger->warn(
                    "SympyReplaceSolutionsVisitor :: Found ambiguous system of equations "
                    "interleaved with {} assignment statements. I do not know what equations go "
                    "before and what "
                    "equations go after the assignment statements. Either put all the equations "
                    "that need to be solved "
                    "in the form: x = f(...) and with distinct variable assignments or do not "
                    "interleave the system with assignments.",
                    interleaves_counter_.n());
            }
        }
    } else {
        node.visit_children(*this);
    }

    const auto& old_statements = node.get_statements();

    ast::StatementVector new_statements;
    new_statements.reserve(2 * old_statements.size());
    for (const auto& old_statement: old_statements) {
        const auto& replacement_ptr = replacements_.find(old_statement);
        if (replacement_ptr != replacements_.end()) {
            new_statements.insert(new_statements.end(),
                                  replacement_ptr->second.begin(),
                                  replacement_ptr->second.end());
            logger->debug("SympyReplaceSolutionsVisitor :: erasing {}", to_nmodl(old_statement));
            for (const auto& replacement: replacement_ptr->second) {
                logger->debug("SympyReplaceSolutionsVisitor :: adding {}", to_nmodl(replacement));
            }
        } else if (to_be_removed_ == nullptr ||
                   to_be_removed_->find(&(*old_statement)) == to_be_removed_->end()) {
            new_statements.emplace_back(std::move(old_statement));
        } else {
            logger->debug("SympyReplaceSolutionsVisitor :: erasing {}", to_nmodl(old_statement));
        }
    }

    if (is_root) {
        if (!pre_solve_statements_.is_all_untagged() || !tmp_statements_.is_all_untagged() ||
            !solutions_.is_all_untagged()) {
            logger->warn(
                "SympyReplaceSolutionsVisitor :: not all solutions were replaced. Adding remaining "
                "statements at the end. Probably something went wrong.");
        }

        pre_solve_statements_.emplace_back_all_statements(new_statements, true);
        tmp_statements_.emplace_back_all_statements(new_statements, true);
        solutions_.emplace_back_all_statements(new_statements, true);
    }

    node.set_statements(std::move(new_statements));
}

void SympyReplaceSolutionsVisitor::visit_diff_eq_expression(ast::DiffEqExpression& node) {
    interleaves_counter_.new_equation(true);

    const auto& statement = std::static_pointer_cast<ast::Statement>(
        node.get_parent()->get_shared_ptr());

    // do not visit if already marked
    if (replacements_.find(statement) != replacements_.end()) {
        return;
    }

    switch (policy_) {
    case ReplacePolicy::VALUE: {
        const auto dependencies = statement_dependencies(node.get_expression()->get_lhs(),
                                                         node.get_expression()->get_rhs());
        const auto& key = dependencies.first;
        const auto& vars = dependencies.second;

        if (solutions_.is_var_assigned_here(key)) {
            ast::StatementVector new_statements;

            pre_solve_statements_.emplace_back_all_statements(new_statements);
            tmp_statements_.emplace_back_all_statements(new_statements);
            solutions_.try_emplace_back_statement(new_statements, key);

            replacements_.emplace(statement, new_statements);
        }
        break;
    }
    case ReplacePolicy::GREEDY: {
        ast::StatementVector new_statements;

        pre_solve_statements_.emplace_back_all_statements(new_statements);
        tmp_statements_.emplace_back_all_statements(new_statements);
        solutions_.emplace_back_next_statements(new_statements, n_next_equations_);

        replacements_.emplace(statement, new_statements);

        break;
    }
    }
}

void SympyReplaceSolutionsVisitor::visit_lin_equation(ast::LinEquation& node) {
    interleaves_counter_.new_equation(true);

    const auto& statement = std::static_pointer_cast<ast::Statement>(
        node.get_parent()->get_shared_ptr());

    const auto dependencies = statement_dependencies(node.get_left_linxpression(),
                                                     node.get_linxpression());

    // do not visit if already marked
    if (replacements_.find(statement) != replacements_.end()) {
        return;
    }

    switch (policy_) {
    case ReplacePolicy::VALUE: {
        const auto dependencies = statement_dependencies(node.get_left_linxpression(),
                                                         node.get_linxpression());
        const auto& key = dependencies.first;
        const auto& vars = dependencies.second;

        if (solutions_.is_var_assigned_here(key)) {
            ast::StatementVector new_statements;

            pre_solve_statements_.emplace_back_all_statements(new_statements);
            tmp_statements_.emplace_back_all_statements(new_statements);
            solutions_.try_emplace_back_statement(new_statements, key);

            replacements_.emplace(statement, new_statements);
        }
        break;
    }
    case ReplacePolicy::GREEDY: {
        ast::StatementVector new_statements;

        pre_solve_statements_.emplace_back_all_statements(new_statements);
        tmp_statements_.emplace_back_all_statements(new_statements);
        solutions_.emplace_back_next_statements(new_statements, n_next_equations_);

        replacements_.emplace(statement, new_statements);
        break;
    }
    }
}

void SympyReplaceSolutionsVisitor::visit_binary_expression(ast::BinaryExpression& node) {
    if (node.get_op().get_value() == ast::BinaryOp::BOP_ASSIGN && node.get_lhs()->is_var_name()) {
        interleaves_counter_.new_equation(false);

        const auto& var =
            std::static_pointer_cast<ast::VarName>(node.get_lhs())->get_name()->get_node_name();
        pre_solve_statements_.tag_dependant_statements(var);
        tmp_statements_.tag_dependant_statements(var);
    }
}


SympyReplaceSolutionsVisitor::SolutionSorter::SolutionSorter(
    const std::vector<std::string>::const_iterator& statements_str_beg,
    const std::vector<std::string>::const_iterator& statements_str_end)
    : statements_(create_statements(statements_str_beg, statements_str_end))
    , tags_(statements_.size(), true) {
    build_maps();
}


void SympyReplaceSolutionsVisitor::SolutionSorter::build_maps() {
    for (size_t ii = 0; ii < statements_.size(); ++ii) {
        const auto& statement = statements_[ii];

        if (statement->is_expression_statement()) {
            const auto& e_statement =
                std::static_pointer_cast<ast::ExpressionStatement>(statement)->get_expression();
            if (e_statement->is_binary_expression()) {
                const auto& bin_exp = std::static_pointer_cast<ast::BinaryExpression>(e_statement);
                const auto& dependencies = statement_dependencies(bin_exp->get_lhs(),
                                                                  bin_exp->get_rhs());

                const auto& key = dependencies.first;
                const auto& vars = dependencies.second;
                if (!key.empty()) {
                    var2statement_.emplace(key, ii);
                    for (const auto& var: vars) {
                        const auto& var_already_inserted = dependency_map_.find(var);
                        if (var_already_inserted != dependency_map_.end()) {
                            dependency_map_[key].insert(var_already_inserted->second.begin(),
                                                        var_already_inserted->second.end());
                            for (const auto& root_var: var_already_inserted->second) {
                                var2dependants_[root_var].insert(ii);
                            }
                        } else {
                            dependency_map_[key].insert(var);
                            var2dependants_[var].insert(ii);
                        }
                    }
                }
            }
        }
    }
}

bool SympyReplaceSolutionsVisitor::SolutionSorter::try_emplace_back_statement(
    ast::StatementVector& new_statements,
    const std::string& var) {
    auto ptr = var2statement_.find(var);
    bool emplaced = false;
    if (ptr != var2statement_.end() && tags_[ptr->second]) {
        const auto ii = ptr->second;
        new_statements.emplace_back(statements_[ii]->clone());
        tags_[ii] = false;
        emplaced = true;
    }
    return emplaced;
}

size_t SympyReplaceSolutionsVisitor::SolutionSorter::emplace_back_next_statements(
    ast::StatementVector& new_statements,
    const size_t n_next_statements) {
    size_t counter = 0;
    for (size_t next_statement_ii = 0;
         next_statement_ii < statements_.size() && counter < n_next_statements;
         ++next_statement_ii) {
        if (tags_[next_statement_ii]) {
            new_statements.emplace_back(statements_[next_statement_ii]->clone());
            tags_[next_statement_ii] = false;
            ++counter;
        }
    }
    return counter;
}

size_t SympyReplaceSolutionsVisitor::SolutionSorter::emplace_back_all_statements(
    ast::StatementVector& new_statements,
    bool is_logger) {
    size_t n = 0;
    for (size_t ii = 0; ii < statements_.size(); ++ii) {
        if (tags_[ii]) {
            new_statements.emplace_back(statements_[ii]->clone());
            tags_[ii] = false;
            ++n;
            if (is_logger) {
                logger->debug("SympyReplaceSolutionsVisitor :: adding {}",
                              to_nmodl(statements_[ii]));
            }
        }
    }
    return n;
}

size_t SympyReplaceSolutionsVisitor::SolutionSorter::tag_dependant_statements(
    const std::string& var) {
    auto ptr = var2dependants_.find(var);
    size_t n = 0;
    if (ptr != var2dependants_.end()) {
        for (const auto ii: ptr->second) {
            tags_[ii] = true;
            ++n;
        }
    }
    return n;
}

void SympyReplaceSolutionsVisitor::SolutionSorter::tag_all_statements() {
    tags_ = std::vector<bool>(tags_.size(), true);
}


}  // namespace visitor
}  // namespace nmodl
