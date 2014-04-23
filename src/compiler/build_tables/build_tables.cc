#include "compiler/build_tables/build_tables.h"
#include <string>
#include <utility>
#include <map>
#include <set>
#include <unordered_map>
#include "compiler/prepared_grammar.h"
#include "compiler/rules/built_in_symbols.h"
#include "compiler/rules/metadata.h"
#include "compiler/rules/repeat.h"
#include "compiler/rules/blank.h"
#include "compiler/rules/seq.h"
#include "compiler/rules/interned_symbol.h"
#include "compiler/build_tables/conflict_manager.h"
#include "compiler/build_tables/item.h"
#include "compiler/build_tables/item_set_closure.h"
#include "compiler/build_tables/item_set_transitions.h"
#include "compiler/build_tables/first_set.h"

namespace tree_sitter {
    using std::pair;
    using std::string;
    using std::map;
    using std::vector;
    using std::set;
    using std::unordered_map;
    using std::make_shared;
    using rules::ISymbol;
    using rules::CharacterSet;

    namespace build_tables {
        class TableBuilder {
            const PreparedGrammar grammar;
            const PreparedGrammar lex_grammar;
            ConflictManager conflict_manager;
            unordered_map<const ParseItemSet, ParseStateId> parse_state_ids;
            unordered_map<const LexItemSet, LexStateId> lex_state_ids;

            set<int> precedence_values_for_item_set(const ParseItemSet &item_set) {
                set<int> result;
                for (const auto &item : item_set)
                    if (item.consumed_symbol_count > 0)
                        result.insert(item.precedence());
                return result;
            }

            void add_shift_actions(const ParseItemSet &item_set, ParseStateId state_id) {
                for (auto &transition : sym_transitions(item_set, grammar)) {
                    const ISymbol &symbol = transition.first;
                    const ParseItemSet &item_set = transition.second;
                    set<int> precedence_values = precedence_values_for_item_set(item_set);

                    auto current_actions = parse_table.states[state_id].actions;
                    auto current_action = current_actions.find(symbol);

                    if (current_action == current_actions.end() ||
                        conflict_manager.resolve_parse_action(symbol, current_action->second, ParseAction::Shift(0, precedence_values))) {
                        ParseStateId new_state_id = add_parse_state(item_set);
                        parse_table.add_action(state_id, symbol, ParseAction::Shift(new_state_id, precedence_values));
                    }
                }
            }

            void add_advance_actions(const LexItemSet &item_set, LexStateId state_id) {
                auto transitions = char_transitions(item_set, grammar);
                for (auto transition : transitions) {
                    CharacterSet rule = transition.first;
                    LexItemSet new_item_set = transition.second;
                    LexStateId new_state_id = add_lex_state(new_item_set);
                    lex_table.add_action(state_id, rule, LexAction::Advance(new_state_id));
                }
            }

            void add_token_start(const LexItemSet &item_set, LexStateId state_id) {
                for (auto &item : item_set)
                    if (item.is_token_start())
                        lex_table.state(state_id).is_token_start = true;
            }

            void add_accept_token_actions(const LexItemSet &item_set, LexStateId state_id) {
                for (LexItem item : item_set) {
                    if (item.is_done()) {
                        auto current_action = lex_table.state(state_id).default_action;
                        auto new_action = LexAction::Accept(item.lhs);
                        if (conflict_manager.resolve_lex_action(current_action, new_action))
                            lex_table.add_default_action(state_id, new_action);
                    }
                }
            }

            void add_reduce_actions(const ParseItemSet &item_set, ParseStateId state_id) {
                for (ParseItem item : item_set) {
                    if (item.is_done()) {
                        ParseAction action = (item.lhs == rules::START()) ?
                            ParseAction::Accept() :
                            ParseAction::Reduce(item.lhs, item.consumed_symbol_count, item.precedence());
                        auto current_actions = parse_table.states[state_id].actions;
                        auto current_action = current_actions.find(item.lookahead_sym);

                        if (current_action == current_actions.end() ||
                            conflict_manager.resolve_parse_action(item.lookahead_sym, current_action->second, action)) {
                            parse_table.add_action(state_id, item.lookahead_sym, action);
                        }
                    }
                }
            }

            rules::rule_ptr after_separators(rules::rule_ptr rule) {
                return rules::Seq::Build({
                    make_shared<rules::Repeat>(CharacterSet({ ' ', '\t', '\n', '\r' }).copy()),
                    make_shared<rules::Metadata>(make_shared<rules::Blank>(), map<rules::MetadataKey, int>({
                        {rules::START_TOKEN, 1},
                    })),
                    rule
                });
            }

            LexItemSet lex_item_set_for_parse_state(const ParseState &state) {
                LexItemSet result;
                for (auto &symbol : state.expected_inputs()) {
                    if (symbol.is_token() && !symbol.is_built_in())
                        result.insert(LexItem(symbol, after_separators(lex_grammar.rule(symbol))));
                    if (symbol == rules::END_OF_INPUT())
                        result.insert(LexItem(symbol, after_separators(CharacterSet({ 0 }).copy())));
                }
                return result;
            }

            void assign_lex_state(ParseStateId state_id) {
                ParseState &state = parse_table.states[state_id];
                state.lex_state_id = add_lex_state(lex_item_set_for_parse_state(state));
            }

            LexStateId add_lex_state(const LexItemSet &item_set) {
                auto pair = lex_state_ids.find(item_set);
                if (pair == lex_state_ids.end()) {
                    LexStateId state_id = lex_table.add_state();
                    lex_state_ids[item_set] = state_id;
                    add_token_start(item_set, state_id);
                    add_advance_actions(item_set, state_id);
                    add_accept_token_actions(item_set, state_id);
                    return state_id;
                } else {
                    return pair->second;
                }
            }

            ParseStateId add_parse_state(const ParseItemSet &item_set) {
                auto pair = parse_state_ids.find(item_set);
                if (pair == parse_state_ids.end()) {
                    ParseStateId state_id = parse_table.add_state();
                    parse_state_ids[item_set] = state_id;
                    add_shift_actions(item_set, state_id);
                    add_reduce_actions(item_set, state_id);
                    assign_lex_state(state_id);
                    return state_id;
                } else {
                    return pair->second;
                }
            }

            void add_error_lex_state() {
                LexItemSet error_item_set;
                for (size_t i = 0; i < lex_grammar.rules.size(); i++) {
                    LexItem item(ISymbol(i, rules::SymbolOptionToken), after_separators(lex_grammar.rules[i].second));
                    error_item_set.insert(item);
                }
                for (size_t i = 0; i < lex_grammar.aux_rules.size(); i++) {
                    LexItem item(ISymbol(i, rules::SymbolOption(rules::SymbolOptionToken|rules::SymbolOptionAuxiliary)), after_separators(lex_grammar.aux_rules[i].second));
                    error_item_set.insert(item);
                }
                error_item_set.insert(LexItem(rules::END_OF_INPUT(), after_separators(CharacterSet({ 0 }).copy())));
                add_advance_actions(error_item_set, LexTable::ERROR_STATE_ID);
                add_accept_token_actions(error_item_set, LexTable::ERROR_STATE_ID);
            }

        public:
            TableBuilder(const PreparedGrammar &grammar,
                         const PreparedGrammar &lex_grammar) :
                grammar(grammar),
                lex_grammar(lex_grammar),
                conflict_manager(ConflictManager(grammar, lex_grammar))
                {}

            void build() {
                auto start_symbol = make_shared<ISymbol>(0);
                ParseItem item(rules::START(), start_symbol, {}, rules::END_OF_INPUT());
                ParseItemSet item_set = item_set_closure(ParseItemSet({ item }), grammar);
                add_parse_state(item_set);
                add_error_lex_state();
            }

            const vector<Conflict> conflicts() {
                return conflict_manager.conflicts();
            };

            ParseTable parse_table;
            LexTable lex_table;
        };

        pair<pair<ParseTable, LexTable>, vector<Conflict>>
        build_tables(const PreparedGrammar &grammar,
                     const PreparedGrammar &lex_grammar) {
            TableBuilder builder(grammar, lex_grammar);
            builder.build();
            return { { builder.parse_table, builder.lex_table }, builder.conflicts() };
        }
    }
}