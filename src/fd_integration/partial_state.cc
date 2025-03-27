#include "partial_state.h"

#include "pr2_proxies.h"

#include "../../axioms.h"
#include "../../utils/hash.h"

#include <algorithm>
#include <iostream>
#include <cassert>

PR2State & PR2State::operator=(const PR2State &other) {
    if (this != &other) {
        vars = other.vars;
    }

    return *this;
}

PR2State::PR2State() {
    vars.assign(PR2.general.num_vars, -1);
}

PR2State::PR2State(std::vector<int> init_vals) {
    vars = init_vals;
}

PR2State::PR2State(const State &state) {
    _allocate(PR2.general.num_vars);
    for (auto var : state)
        vars[var.get_variable().get_id()] = var.get_value();
}

PR2State::PR2State(const PR2State &state) {
    vars = state.vars;
}

PR2State::~PR2State() {}

int PR2State::size() const {
    int count = 0;
    for (unsigned i = 0; i < PR2.general.num_vars; i++) {
        if (vars[i] != -1)
            count++;
    }
    return count;
}

vector< pair<int,int> > * PR2State::varvals() {
    if (NULL == _varvals) {
        _varvals = new vector< pair<int,int> >();
        for (int i = 0; i < (int) PR2.general.num_vars; i++)
            if (-1 != vars[i])
                _varvals->push_back(make_pair(i,vars[i]));
    }
    return _varvals;
}

bool PR2State::triggers(const EffectProxy &effect) {
    for (auto cond : effect.get_conditions()) {
        if (vars[cond.get_variable().get_id()] != cond.get_value())
            return false;
    }
    return true;
}

bool PR2State::triggers(const FactProxy &fact) {
    if (vars[fact.get_variable().get_id()] != fact.get_value())
        return false;
    return true;
}

PR2State * PR2State::progress(const PR2OperatorProxy &op) {

    assert(!op.is_axiom());

    PR2State * next = new PR2State(*this);

    for (auto eff : op.get_all_effects()) {
        if (triggers(eff))
            (*next)[eff.get_fact().get_variable().get_id()] = eff.get_fact().get_value();
    }

    // PR2 TODO : This is disabled since we cannot handle domains with axioms,
    //      leaving it in slows us down.
    //g_axiom_evaluator->evaluate(*this);

    return next;

}

PR2State * PR2State::regress(const PR2OperatorProxy &op, PR2State *context) {

    assert(!op.is_axiom());
    assert(NULL != context);

    // vector<int> relevant_axioms = {};
    // set<int> added = {};

    PR2State * prev = new PR2State(*this);

    std::set<int> seen;

    // Remove all of the effect settings
    for (auto eff : op.get_all_effects()) {
        if (context->triggers(eff)) {
            int var = eff.get_fact().get_variable().get_id();
            int val = eff.get_fact().get_value();

            seen.insert(var);

            bool inconsistent = (vars[var] != -1) && (vars[var] != val);

            if (inconsistent) {
                cout << "\n\n !! Error: Inconsistent regression !!\n" << endl;
                // Dump the effect
                cout << "Effect: " << endl;
                op.dump();

                for (auto cond : eff.get_conditions())
                    cout << "  " << cond.get_variable().get_id() << " = " << cond.get_value() << endl;
            }

            assert(!inconsistent);
            (*prev)[eff.get_fact().get_variable().get_id()] = -1;
        }
    }

    // Remove all of the prevail effects
    for (auto pre : op.get_preconditions()) {
        int var = pre.get_variable().get_id();
        int val = pre.get_value();
        if (0 == seen.count(var) && context->triggers(pre)) {
            (*prev)[var] = -1;
        }
    }
    
    // Assign the values from the context that are mentioned in conditions
    for (auto var : *(PR2.general.conditional_mask[op.nondet_index]))
        (*prev)[var] = (*context)[var];

    // Add all of the precondition conditions
    for (FactProxy pre : op.get_preconditions()) {
        //if the precondition is an derived predicate, find relevant variables and undefine
        if (pre.get_variable().is_derived()) {
            // relevant_axioms.push_back(pre.get_pair().var);   
        //else set to context value
        } else {
            (*prev)[pre.get_pair().var] = pre.get_pair().value;
        }
    }

    // cout << prev->get_unpacked_values() << endl;

    // set<int> potential_untouchables = get_relevant_basic_variables(relevant_axioms);

    // set<int> intermediate;

    // std::set_difference( 
    //     potential_untouchables.begin(), potential_untouchables.end(),
    //     (*prev).untouchables.begin(), (*prev).untouchables.end(), 
    //     inserter(intermediate, intermediate.begin()));

    // (*prev).untouchables = intermediate;

    // cout << prev->get_unpacked_values() << endl;

    // for (int index : (*prev).untouchables)
    // {
    //     int new_index = PR2.proxy->get_variables()[index].get_id();
    //     (*prev)[new_index] = (*context)[new_index];
    // }
    
    // set<int> step1 = {};
    // set<int> step2 = {};

    // std::set_union(potential_untouchables.begin(), potential_untouchables.end(),
    //           (*context).untouchables.begin(), (*context).untouchables.end(),
    //           inserter(step1,step1.begin()));

    // std::set_difference( 
    //     step1.begin(), step1.end(),
    //     added.begin(), added.end(), 
    //     inserter(step2, step2.begin())); 

    // (*prev).untouchables = step2;

    // //Undefine all axioms
    // for (int i = 0; i < vars.size(); i++) {
    //     VariablesProxy variables = PR2.proxy->get_variables();
    //     if (variables[i].is_derived())
    //     {
    //         (*prev)[i] = -1;
    //     }
    // }

    return prev;
}

set<int> get_relevant_basic_variables(vector<int> target_axioms) {
    if (target_axioms.empty()) {
        return {};
    }

    AxiomsProxy axioms = PR2.proxy->get_axioms();
    set<int> relevant_basics = {};
    vector<int> seen = target_axioms;

    while (!target_axioms.empty()) {
        int target = target_axioms.back();
        target_axioms.pop_back();

        for (OperatorProxy op : axioms) {
            if (op.get_effects()[0].get_fact().get_pair().var == target) {
                for (FactProxy pre : op.get_effects()[0].get_conditions()) {
                    int index = pre.get_variable().get_id();
                    if (pre.get_variable().get_axiom_layer() != -1) {
                        if (find(seen.begin(), seen.end(), index) == seen.end()) {
                            target_axioms.push_back(index);
                            seen.push_back(index);
                        }
                    } else {
                        if (find(relevant_basics.begin(), relevant_basics.end(), index) == relevant_basics.end()) {
                            relevant_basics.insert(index);
                        }
                    }
                }
            }
        }
    }
    return relevant_basics;
}


bool PR2State::entails(const PR2State &other) {
    for (unsigned i = 0; i < PR2.general.num_vars; i++)
        if ((other[i] != -1) && (vars[i] != other[i]))
            return false;
    return true;
}

bool PR2State::consistent_with(const PR2State &other) {
    for (unsigned i = 0; i < PR2.general.num_vars; i++)
        if ((other[i] != -1) && (vars[i] != -1) && (vars[i] != other[i]))
            return false;
    return true;
}

void PR2State::combine_with(const PR2State &other) {
    if (_varvals)
        delete _varvals;
    for (unsigned i = 0; i < PR2.general.num_vars; i++) {
        assert((vars[i] == -1) || (other[i] == -1) || (vars[i] == other[i]));
        if (other[i] != -1)
            vars[i] = other[i];
    }
}

void PR2State::dump_pddl() const {
    if (PR2.logging.disable_state_dump) {
        cout << "  <disabled>" << endl;
        return;
    }
    for (unsigned i = 0; i < PR2.general.num_vars; i++) {
        if (-1 != vars[i]) {
            cout << PR2.proxy->get_fact_name(i, vars[i]) << endl;
        }
    }
}

void PR2State::dump_fdr() const {
    if (PR2.logging.disable_state_dump) {
        cout << "  <disabled>" << endl;
        return;
    }
    for (unsigned i = 0; i < PR2.general.num_vars; i++) {
        if (-1 != vars[i]) {
            cout << "  #" << i << " [" << PR2.proxy->get_variable_name(i) << "] -> " << vars[i] << endl;
        }
    }
}

void PR2State::record_snapshot(ofstream &outfile, string indent) {
    outfile << indent << "\"" << this << "\": [" << endl;
    bool first = true;
    for (unsigned i = 0; i < PR2.general.num_vars; i++) {
        if (-1 != vars[i]) {
            if (first)
                first = false;
            else
                outfile << "," << endl;
            outfile << indent << "  \"" << PR2.proxy->get_fact_name(i, vars[i]) << "\"";
        }
    }
    outfile << endl << indent << "]";
}


// TODO: Confirm these comparisons are ok.

bool PR2State::operator==(const PR2State &other) const {
    // unsigned size = PR2.general.num_vars;
    // return ::equal(vars, vars + size, other.vars);
    return vars == other.vars;
}

bool PR2State::operator<(const PR2State &other) const {
    // unsigned size = PR2.general.num_vars;
    // return ::lexicographical_compare(vars, vars + size,
    //                                  other.vars, other.vars + size);
    return vars < other.vars;
}



// TODO: Confirm that we no longer need a hash on PR2States
// size_t PR2State::hash() const {
//     // TODO: Cache this
//     utils::HashState hash_state;
//     utils::feed(hash_state, vars);
//     return hash_state.get_hash32();
// }
