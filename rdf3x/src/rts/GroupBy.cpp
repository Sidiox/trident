#include <rts/operator/GroupBy.hpp>
#include <rts/operator/PlanPrinter.hpp>

#include <algorithm>

GroupBy::GroupBy(Operator* child,
        std::map<unsigned, Register*> bindings,
        std::vector<unsigned> regs,
        bool distinct,
        double expectedOutputCardinality) : Operator(expectedOutputCardinality), child(child), bindings(bindings), regs(regs), distinct(distinct) {
    // initialize keys: index is position number, value is register number
    for (auto v = bindings.begin(); v != bindings.end(); v++) {
        keys.push_back(v->first);
    }
}

/// Destructor
GroupBy::~GroupBy() {
    delete child;
}

std::vector<unsigned> GroupBy::calculateFieldsToCompare(
        std::vector<unsigned> &keys,
        std::vector<unsigned> &regs) {
    std::vector<unsigned> fields;
    // regs contains the register numbers on which to sort, in order.
    // We need to find them in the keys vector, and remember the index.
    for (auto v = regs.begin(); v != regs.end(); v++) {
        bool found = false;
        for (unsigned i = 0; i < keys.size(); i++) {
            if (keys[i] == *v) {
                // Found it, remember the index
                fields.push_back(i);
                found = true;
                break;
            }
        }
        if (! found) {
            // This should not happen, obviously. Trying to sort on a register that is not
            // present???
            throw 10;
        }
    }
    return fields;
}

struct ValueSorter {
    // The sorter requires a list of indices in sort-field order
    std::vector<unsigned> fields;
    ValueSorter(std::vector<unsigned> &fields) : fields(fields) {
    }

    bool operator() (const std::unique_ptr<std::vector<uint64_t>> &v1,
            const std::unique_ptr<std::vector<uint64_t>> &v2) {
        for (auto v = fields.begin(); v != fields.end(); v++) {
            if ((*v1)[*v] < (*v2)[*v]) {
                return true;
            }
            if ((*v1)[*v] > (*v2)[*v]) {
                return false;
            }
        }
        // Don't care ...
        return false;
    }
};

bool GroupBy::sameThanPrevious(uint64_t index) {
    for(uint8_t i = 0; i < fields.size(); ++i) {
        unsigned fieldId = fields[i];
        if (values[index-1]->at(fieldId) != values[index]->at(fieldId)) {
            return false;
        }
    }
    return true;
}

/// Produce the first tuple
uint64_t GroupBy::first() {
    observedOutputCardinality=0;
    uint64_t cnt = child->first();

    while (cnt > 0) {
        std::unique_ptr<std::vector<uint64_t>> tuple = std::unique_ptr<
            std::vector<uint64_t>>(new std::vector<uint64_t>());
        // Get value from bindings
        for (int i = 0; i < bindings.size(); i++) {
            tuple->push_back(bindings[keys[i]]->value);
        }
        // And save count if needed
        if (!distinct) {
            tuple->push_back(cnt);
        }
        // Store into values vector
        values.push_back(std::move(tuple));
        // Get next value from child
        cnt = child->next();
    }

    // sort values vector according to regs
    fields = calculateFieldsToCompare(keys, regs);
    std::sort(values.begin(), values.end(), ValueSorter(fields));

    // initialize
    index = 1;

    // Restore first value into bindings
    if (!values.empty()) {
        for (int i = 0; i < bindings.size(); i++) {
            bindings[keys[i]]->value = (*values[0])[i];
        }
        // Restore count if needed
        cnt = distinct ? 1 : (*values[0])[bindings.size()];

        // Destroy saved value, it is no longer needed
        //delete values[0];
        //values[0] = NULL;

        observedOutputCardinality += cnt;
        return cnt;
    } else {
        return 0;
    }
}

/// Produce the next tuple
uint64_t GroupBy::next() {
    //if "distinct == 1" then move to the first row with a different key
    while (distinct && index < values.size() && sameThanPrevious(index)) {
        index++;
    }

    if (index >= values.size()) {
        // No more values available
        return 0;
    }

    // Restore count if needed
    uint64_t sz = distinct ? 1 : (*values[index])[bindings.size()];

    // Restore value into bindings
    for (int i = 0; i < bindings.size(); i++) {
        bindings[keys[i]]->value = (*values[index])[i];
    }

    // Destroy saved value, it is no longer needed
    //delete values[index];
    //values[index] = NULL;

    // Prepare for next next() call.
    index++;

    // and return
    observedOutputCardinality += sz;
    return sz;
}

/// Print the operator tree. Debugging only.
void GroupBy::print(PlanPrinter& out) {
    out.beginOperator("GroupBy",expectedOutputCardinality, observedOutputCardinality);
    child->print(out);
    out.endOperator();
}

/// Add a merge join hint
void GroupBy::addMergeHint(Register* reg1,Register* reg2) {
    child->addMergeHint(reg1, reg2);
}

/// Register parts of the tree that can be executed asynchronous
void GroupBy::getAsyncInputCandidates(Scheduler& scheduler) {
    child->getAsyncInputCandidates(scheduler);
}
