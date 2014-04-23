#include "compiler_spec_helper.h"
#include "compiler/build_tables/rule_transitions.h"
#include "compiler/rules/metadata.h"

using namespace rules;
using namespace build_tables;

template<typename K>
class rule_map : public map<K, rule_ptr> {
public:
    bool operator==(const map<K, rule_ptr> &other) const {
        if (this->size() != other.size()) return false;
        for (const auto &pair : *this) {
            auto other_pair = other.find(pair.first);
            if (other_pair == other.end()) return false;
            if (!pair.second->operator==(*other_pair->second)) return false;
        }
        return true;
    }

    rule_map(const initializer_list<pair<const K, rule_ptr>> &list) : map<K, rule_ptr>(list) {}
};

START_TEST

describe("rule transitions", []() {
    it("handles symbols", [&]() {
        AssertThat(
            sym_transitions(i_sym(1)),
            Equals(rule_map<ISymbol>({
                { ISymbol(1), blank() }
            })));
    });

    it("handles choices", [&]() {
        AssertThat(
            sym_transitions(choice({ i_sym(1), i_sym(2) })),
            Equals(rule_map<ISymbol>({
                { ISymbol(1), blank() },
                { ISymbol(2), blank() }
            })));
    });

    it("handles sequences", [&]() {
        AssertThat(
            sym_transitions(seq({ i_sym(1), i_sym(2) })),
            Equals(rule_map<ISymbol>({
                { ISymbol(1), i_sym(2) }
            })));
    });

    it("handles long sequences", [&]() {
        AssertThat(
            sym_transitions(seq({
                i_sym(1),
                i_sym(2),
                i_sym(3),
                i_sym(4)
            })),
            Equals(rule_map<ISymbol>({
                { ISymbol(1), seq({ i_sym(2), i_sym(3), i_sym(4) }) }
            })));
    });

    it("handles sequences whose left sides can be blank", [&]() {
        AssertThat(
            sym_transitions(seq({
                choice({
                    i_sym(1),
                    blank(),
                }),
                seq({
                    i_sym(1),
                    i_sym(2)
                })
            })), Equals(rule_map<ISymbol>({
                { ISymbol(1), choice({ seq({ i_sym(1), i_sym(2) }), i_sym(2), }) }
            })));
    });

    it("handles choices with common starting symbols", [&]() {
        AssertThat(
            sym_transitions(
                choice({
                    seq({ i_sym(1), i_sym(2) }),
                    seq({ i_sym(1), i_sym(3) }) })),
            Equals(rule_map<ISymbol>({
                { ISymbol(1), choice({ i_sym(2), i_sym(3) }) }
            })));
    });

    it("handles characters", [&]() {
        AssertThat(
            char_transitions(character({ '1' })),
            Equals(rule_map<CharacterSet>({
                { CharacterSet({ '1' }), blank() }
            })));
    });

    it("handles strings", [&]() {
        AssertThat(
            char_transitions(str("bad")),
            Equals(rule_map<CharacterSet>({
                { CharacterSet({ 'b' }), seq({ character({ 'a' }), character({ 'd' }) }) }
            })));
    });

    it("handles patterns", [&]() {
        AssertThat(
            char_transitions(pattern("a|b")),
            Equals(rule_map<CharacterSet>({
                { CharacterSet({ 'a' }), blank() },
                { CharacterSet({ 'b' }), blank() }
            })));
    });

    it("handles choices between overlapping character sets", [&]() {
        AssertThat(
            char_transitions(choice({
                seq({
                    character({ {'a', 's'} }),
                    sym("x") }),
                seq({
                    character({ { 'm', 'z' } }),
                    sym("y") }) })),
            Equals(rule_map<CharacterSet>({
                { CharacterSet({ {'a','l'} }), sym("x") },
                { CharacterSet({ {'m','s'} }), choice({ sym("x"), sym("y") }) },
                { CharacterSet({ {'t','z'} }), sym("y") },
            })));
    });

    it("handles choices between a subset and a superset of characters", [&]() {
        AssertThat(
            char_transitions(choice({
                seq({
                    character({ {'a', 'c'} }),
                    sym("x") }),
                seq({
                    character({ { 'a', 'z' } }),
                    sym("y") }) })),
            Equals(rule_map<CharacterSet>({
                { CharacterSet({ {'a', 'c'} }), choice({ sym("x"), sym("y") }) },
                { CharacterSet({ {'d', 'z'} }), sym("y") },
            })));

        AssertThat(
            char_transitions(choice({
                seq({
                    character({ { 'a', 'z' } }),
                    sym("x") }),
                seq({
                    character({ {'a', 'c'} }),
                    sym("y") }) })),
            Equals(rule_map<CharacterSet>({
                { CharacterSet({ {'a', 'c'} }), choice({ sym("x"), sym("y") }) },
                { CharacterSet({ {'d', 'z'} }), sym("x") },
            })));

    });

    it("handles blanks", [&]() {
        AssertThat(char_transitions(blank()), Equals(rule_map<CharacterSet>({})));
    });

    it("handles repeats", [&]() {
        rule_ptr rule = repeat(str("ab"));
        AssertThat(
            char_transitions(rule),
            Equals(rule_map<CharacterSet>({
                {
                    CharacterSet({ 'a' }),
                    seq({
                        character({ 'b' }),
                        rule,
                    })
                }})));

        rule = repeat(str("a"));
        AssertThat(
            char_transitions(rule),
            Equals(rule_map<CharacterSet>({
                { CharacterSet({ 'a' }), rule }
            })));
    });

    it("preserves metadata", [&]() {
        map<MetadataKey, int> metadata_value({
            { PRECEDENCE, 5 }
        });

        rule_ptr rule = make_shared<Metadata>(seq({ i_sym(1), i_sym(2) }), metadata_value);
        AssertThat(
            sym_transitions(rule),
            Equals(rule_map<ISymbol>({
                { ISymbol(1), make_shared<Metadata>(i_sym(2), metadata_value)},
            })));
    });

    describe("regression tests (somewhat redundant, should maybe be deleted later)", []() {
        it("handles sequences that start with repeating characters", [&]() {
            auto rule = seq({
                choice({
                    repeat(character({ '"' }, false)),
                    blank(),
                }),
                character({ '"' }),
            });

            AssertThat(char_transitions(rule), Equals(rule_map<CharacterSet>({
                { CharacterSet({ '"' }).complement(), seq({
                    repeat(character({ '"' }, false)),
                    character({ '"' }), }) },
                { CharacterSet({ '"' }), blank() },
            })));
        });
    });
});


END_TEST