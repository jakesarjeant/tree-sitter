#include "compiler/lex_table.h"
#include "compiler/rules/interned_symbol.h"

namespace tree_sitter {
    using std::string;
    using std::to_string;
    using std::map;
    using std::set;
    using rules::ISymbol;
    using rules::CharacterSet;

    LexAction::LexAction() :
        type(LexActionTypeError),
        symbol(ISymbol(-1)),
        state_index(-1) {}

    LexAction::LexAction(LexActionType type, size_t state_index, ISymbol symbol) :
        type(type),
        symbol(symbol),
        state_index(state_index) {}

    LexAction LexAction::Error() {
        return LexAction(LexActionTypeError, -1, ISymbol(-1));
    }

    LexAction LexAction::Advance(size_t state_index) {
        return LexAction(LexActionTypeAdvance, state_index, ISymbol(-1));
    }

    LexAction LexAction::Accept(ISymbol symbol) {
        return LexAction(LexActionTypeAccept, -1, symbol);
    }

    bool LexAction::operator==(const LexAction &other) const {
        return
        (type == other.type) &&
        (state_index == other.state_index) &&
        (symbol == other.symbol);
    }

    std::ostream& operator<<(std::ostream &stream, const LexAction &action) {
        switch (action.type) {
            case LexActionTypeError:
                return stream << string("#<error>");
            case LexActionTypeAccept:
                return stream << string("#<accept ") + to_string(action.symbol.index) + ">";
            case LexActionTypeAdvance:
                return stream << string("#<advance ") + to_string(action.state_index) + ">";
            default:
                return stream;
        }
    }

    set<CharacterSet> LexState::expected_inputs() const {
        set<CharacterSet> result;
        for (auto &pair : actions)
            result.insert(pair.first);
        return result;
    }

    LexStateId LexTable::add_state() {
        states.push_back(LexState());
        return states.size() - 1;
    }

    LexState & LexTable::state(LexStateId id) {
        if (id < 0)
            return error_state;
        else
            return states[id];
    }

    void LexTable::add_action(LexStateId id, CharacterSet match, LexAction action) {
        state(id).actions[match] = action;
    }

    void LexTable::add_default_action(LexStateId id, LexAction action) {
        state(id).default_action = action;
    }

    const LexStateId LexTable::ERROR_STATE_ID = -1;
}