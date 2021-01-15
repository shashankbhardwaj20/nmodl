/*************************************************************************
 * Copyright (C) 2018-2020 Blue Brain Project
 *
 * This file is part of NMODL distributed under the terms of the GNU
 * Lesser General Public License. See top-level LICENSE file for details.
 *************************************************************************/

#pragma once

/**
 * \file
 * \brief \copybrief nmodl::visitor::SympyReplaceSolutionsVisitor
 */

#include "visitors/ast_visitor.hpp"

#include <algorithm>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace nmodl {
namespace visitor {

/**
 * @addtogroup visitor_classes
 * @{
 */


/**
 * \class SympyReplaceSolutionsVisitor
 * \brief Replace statements in \p node with pre_solve_statements, tmp_statements, and solutions
 *
 * The goal is to replace statements with \ref solutions_ in place. In this was we can allow (to
 * some extent) the use fo control flow blocks and assignments. \ref pre_solve_statements are added
 * in front of the replaced statements in case their variable need updating. \ref SolutionSorter
 * keeps track of what needs updating.
 *
 * We employ a multi-step approach:
 *
 * - try to replace all the statements that are assignment matching by assigned variable
 * - try to replace all the statements with a greedy approach. When we find a statement that needs
 * replacing we take the next solution that was not yet used
 * - add all the remaining statements at the end
 */
class SympyReplaceSolutionsVisitor: public AstVisitor {
  private:
    enum class ReplacePolicy {
        VALUE = 0,   //!< Replace statements matching by lhs varName
        GREEDY = 1,  //!< Replace statements greedily
    };

  public:
    /// Empty ctor
    SympyReplaceSolutionsVisitor() = delete;

    /// Default constructor
    SympyReplaceSolutionsVisitor(const std::vector<std::string>& pre_solve_statements,
                                 const std::vector<std::string>& solutions,
                                 const std::unordered_set<ast::Statement*>& to_be_removed);


    void visit_statement_block(ast::StatementBlock& node) override;
    void visit_diff_eq_expression(ast::DiffEqExpression& node) override;
    void visit_lin_equation(ast::LinEquation& node) override;
    void visit_binary_expression(ast::BinaryExpression& node) override;


  private:
    /**
     * \struct SolutionSorter
     * \brief Sorts and maps statements to variables keeping track of what needs updating
     *
     * This is a multi-purpose object that:
     *
     * - keeps track of what was already updated
     * - decides what statements need updating in case there was a variable assignment (i.e. \f a =
     * 3 \f)
     * - builds the statements from a vector of strings
     *
     */
    struct SolutionSorter {
        /// Empty ctor
        SolutionSorter() = default;

        /// Standard ctor
        SolutionSorter(const std::vector<std::string>::const_iterator& statements_str_beg,
                       const std::vector<std::string>::const_iterator& statements_str_end);

        /**
         * Here we construct a map variable -> affected equations. In other words this map tells me
         * what equations need to be updated when I change a particular variable. To do that we
         * build a a graph of dependencies var -> vars and in the mean time we reduce it to the root
         * variables. This is ensured by the fact that the tmp variables are sorted so that the next
         * tmp variable may depend on the previous one. Since it is a relation of equivalence (if an
         * equation depends on a variable, it needs to be updated if the variable changes), we build
         * the two maps at the same time.
         *
         * An example:
         *
         *  - \f tmp0 = x + a \f
         *  - \f tmp1 = tmp0 + b \f
         *  - \f tmp2 = y \f
         *
         * dependency_map_ should be (the order of the equation is unimportant since we are building
         * a map):
         *
         * - tmp0 : x, a
         * - tmp1 : x, a, b
         * - tmp2 : y
         *
         * and the var2statement_ map should be (the order of the following equations is unimportant
         * since we are building a map. The number represents the index of the original equations):
         *
         * - x : 0, 1
         * - y : 2
         * - a : 0, 1
         * - b : 1
         */
        void build_maps();

        /// Check if one of the statements assigns this variable (i.e. \f x' = f(x, y, x) \f)
        inline bool is_var_assigned_here(const std::string& var) const {
            return var2statement_.find(var) != var2statement_.end();
        }

        /// Check if all the statements found their position
        inline bool is_all_untagged() const {
            return std::find(tags_.begin(), tags_.end(), true) == tags_.end();
        }
        /**
         * \brief Look for \p var in \ref var2statement_ and emplace back that statement in \p
         * new_statements
         *
         * If there is no \p var key in \ref var2statement_, return
         */
        bool try_emplace_back_statement(ast::StatementVector& new_statements,
                                        const std::string& var);


        /// Emplace back the first statement in \ref statements that is marked for updating in \ref
        /// tags_
        bool emplace_back_next_statement(ast::StatementVector& new_statements);

        /// Emplace back all the statements that are marked for updating in \ref tags_
        size_t emplace_back_all_statements(ast::StatementVector& new_statements);

        /**
         * \brief Tag all the statements that depend on \p var for updating
         *
         * This is necessary when an assignment has invalidated this variable
         */
        size_t tag_dependant_statements(const std::string& var);

        /// Mark that all the statements need updating (probably unused)
        void tag_all_statements();

        // TODO remove
        void print();

        /**
         * \brief var -> (depends on) vars
         *
         * The statements are assignments. Thus, in general, an assigned variable depends on
         * the variables in the rhs of the assignment operator. This map keeps track of this
         * dependency
         */
        std::unordered_map<std::string, std::unordered_set<std::string>> dependency_map_;

        /**
         * \brief var -> (statements that depend on) statements
         *
         * This the "reverse" of \ref dependency_map_. Given a certain variable it provides
         * the statements that depend on it. It is a set because we want to print them in
         * order and we do not want duplicates. The value is the index in \ref statements_ or \ref
         * tags_
         */
        std::unordered_map<std::string, std::set<size_t>> var2dependants_;

        /**
         * \brief var -> statement that sets that var
         *
         * Given a certain variable we get the statement that sets that variable as index in \ref
         * statements_ or \ref tags_
         */
        std::unordered_map<std::string, size_t> var2statement_;

        /// Vector of statements
        std::vector<std::shared_ptr<ast::Statement>> statements_;

        /**
         * \brief Keeps track of what statements need updating
         *
         * This vector is always as long as \ref statements_. \ref tags_[ii] == true means that \ref
         * statements_[ii] needs updating
         */
        std::vector<bool> tags_;
    };

    /// Update state variable statements (i.e. \f old_x = x \f)
    SolutionSorter pre_solve_statements_;

    /// tmp statements that appear with --cse (i.e. \f tmp0 = a \f)
    SolutionSorter tmp_statements_;

    /// solutions that we want to replace
    SolutionSorter solutions_;

    /**
     * \brief Replacements found by the visitor
     *
     * The keys are the old_statements that need replacing with the new ones (the
     * value). Since there are \ref pre_solve_statements_ and \ref tmp_statements_; it is in general
     * a replacement of 1 : n statements
     */
    std::unordered_map<std::shared_ptr<ast::Statement>, ast::StatementVector> replacements_;

    /// Used to notify to visit_statement_block was called by the user (or another visitor) or
    /// re-called in a nested block
    bool is_statement_block_root_ = true;

    /// Replacement policy used by the various visitors
    ReplacePolicy policy_;

    /// group of old statements that need replacing
    const std::unordered_set<ast::Statement*>* to_be_removed_;
};

/** @} */  // end of visitor_classes

}  // namespace visitor
}  // namespace nmodl