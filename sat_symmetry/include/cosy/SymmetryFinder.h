// Copyright 2017 Hakan Metin - LIP6

#ifndef INCLUDE_COSY_SYMMETRYFINDER_H_
#define INCLUDE_COSY_SYMMETRYFINDER_H_

#include <string>

#include "cosy/CNFModel.h"
#include "cosy/CNFGraph.h"
#include "cosy/Group.h"
#include "cosy/Stats.h"


namespace cosy {

struct SymmetryFinderInfo {
    explicit SymmetryFinderInfo(Group *g, unsigned int n) :
        group(g),
        num_vars(n) {}
    Group *group;
    unsigned int num_vars;
};

class SymmetryFinder {
 public:
    enum Automorphism {
        BLISS,
        SAUCY,
    };

    virtual ~SymmetryFinder() {}

    virtual void findAutomorphism(Group *group) = 0;
    virtual std::string toolName() const = 0;

    static SymmetryFinder* create(const CNFModel& model,
                                  SymmetryFinder::Automorphism tool);

    void printStats() const {
        Printer::printStat("Automorhism tool", toolName());
        _stats.print();
    }

 protected:
    unsigned int _num_vars;
    CNFGraph _graph;

    explicit SymmetryFinder(const CNFModel& model) {
        _num_vars = model.numberOfVariables();
        _graph.assign(model);
    }

    struct Stats : public StatsGroup {
        Stats() : StatsGroup("Symmetry Finder"),
                  find_time("Automorphism time", this) {}
        TimeDistribution find_time;
    };
    Stats _stats;
};

}  // namespace cosy

#endif  // INCLUDE_COSY_SYMMETRYFINDER_H_
/*
 * Local Variables:
 * mode: c++
 * indent-tabs-mode: nil
 * End:
 */
