// Copyright 2017 Hakan Metin - LIP6

#include "cosy/BlissSymmetryFinder.h"

#include "bliss/graph.hh"

namespace cosy {

static void
on_automorphim(void* arg, const unsigned int n, const unsigned int* aut) {
  SymmetryFinderInfo *info = static_cast<SymmetryFinderInfo*>(arg);
  Group *group = info->group;
  unsigned int num_vars = info->num_vars;
  std::unique_ptr<Permutation> permutation(new Permutation(num_vars));
  LiteralIndex index;
  std::vector<bool> seen(n);

    for (unsigned int i = 0; i < n; ++i) {
        if (i == aut[i] || seen[i])
            continue;

        index = LiteralIndex(node2Literal(i, num_vars));
        if (index != kNoLiteralIndex)
            permutation->addToCurrentCycle(Literal(index));

        seen[i] = true;

        for (unsigned int j = aut[i]; j != i; j = aut[j]) {
            seen[j] = true;
            index = LiteralIndex(node2Literal(j, num_vars));
            if (index != kNoLiteralIndex)
                permutation->addToCurrentCycle(Literal(index));
        }
        permutation->closeCurrentCycle();
    }
    group->addPermutation(std::move(permutation));
}

void BlissSymmetryFinder::findAutomorphism(Group *group) {
    SCOPED_TIME_STAT(&_stats.find_time);

    unsigned int n = _graph.numberOfNodes();

    std::unique_ptr<bliss::Graph> g(new bliss::Graph(n));
    bliss::Stats stats;

    for (unsigned int i = 0; i < n; i++)
        g->change_color(i, _graph.color(i));

    for (unsigned int i = 0; i < n; i++)
        for (const unsigned int& x : _graph.neighbour(i))
            g->add_edge(i, x);

    SymmetryFinderInfo info(group, _num_vars);

    g->find_automorphisms(stats, &on_automorphim, static_cast<void*>(&info));
}


}  // namespace cosy
