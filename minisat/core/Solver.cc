/***************************************************************************************[Solver.cc]
Copyright (c) 2003-2006, Niklas Een, Niklas Sorensson
Copyright (c) 2007-2010, Niklas Sorensson

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and
associated documentation files (the "Software"), to deal in the Software without restriction,
including without limitation the rights to use, copy, modify, merge, publish, distribute,
sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or
substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT
NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT
OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
**************************************************************************************************/

/*
 * This code has been modified as to implement Symmetry Propagation For Sat (SPFS).
 * These modifications are copyrighted to Jo Devriendt (2011-2012), student of the University of Leuven.
 *
 * The same license as above applies concerning the code containing symmetry modifications.
 */

#include <math.h>

#include "minisat/mtl/Alg.h"
#include "minisat/mtl/Sort.h"
#include "minisat/utils/System.h"
#include "minisat/core/Solver.h"

using namespace Minisat;

//=================================================================================================
// Options:


static const char* _cat = "CORE";

static DoubleOption  opt_var_decay         (_cat, "var-decay",   "The variable activity decay factor",            0.95,     DoubleRange(0, false, 1, false));
static DoubleOption  opt_clause_decay      (_cat, "cla-decay",   "The clause activity decay factor",              0.999,    DoubleRange(0, false, 1, false));
static DoubleOption  opt_random_var_freq   (_cat, "rnd-freq",    "The frequency with which the decision heuristic tries to choose a random variable", 0, DoubleRange(0, true, 1, true));
static DoubleOption  opt_random_seed       (_cat, "rnd-seed",    "Used by the random variable selection",         91648253, DoubleRange(0, false, HUGE_VAL, false));
static IntOption     opt_ccmin_mode        (_cat, "ccmin-mode",  "Controls conflict clause minimization (0=none, 1=basic, 2=deep)", 2, IntRange(0, 2));
static IntOption     opt_phase_saving      (_cat, "phase-saving", "Controls the level of phase saving (0=none, 1=limited, 2=full)", 2, IntRange(0, 2));
static BoolOption    opt_rnd_init_act      (_cat, "rnd-init",    "Randomize the initial activity", false);
static BoolOption    opt_luby_restart      (_cat, "luby",        "Use the Luby restart sequence", true);
static IntOption     opt_restart_first     (_cat, "rfirst",      "The base restart interval", 100, IntRange(1, INT32_MAX));
static DoubleOption  opt_restart_inc       (_cat, "rinc",        "Restart interval increase factor", 2, DoubleRange(1, false, HUGE_VAL, false));
static DoubleOption  opt_garbage_frac      (_cat, "gc-frac",     "The fraction of wasted memory allowed before a garbage collection is triggered",  0.20, DoubleRange(0, false, HUGE_VAL, false));
static BoolOption    opt_storing	       (_cat, "storing",     "Store generated symmetry clauses for future use", true);
static BoolOption    opt_inverting	       (_cat, "inverting-opt","Adjust initial variable order to make inverting symmetries faster", false);
static BoolOption    opt_inactive	       (_cat, "inactive-opt","Conduct symmetry propagation for inactive symmetries", false);

// static BoolOption    opt_esbp_begin	       (_cat, "esbp-begin","Conduct symmetry propagation for inactive symmetries", false);
static BoolOption    opt_esbp_end	       (_cat, "esbp-end","Conduct symmetry propagation for inactive symmetries", true);


//=================================================================================================
// Constructor/Destructor:


Solver::Solver() :

    // Parameters (user settable):
    //
     max_decision_level(0)
  ,  verbosity        (0)
  , var_decay        (opt_var_decay)
  , clause_decay     (opt_clause_decay)
  , random_var_freq  (opt_random_var_freq)
  , random_seed      (opt_random_seed)
  , luby_restart     (opt_luby_restart)
  , ccmin_mode       (opt_ccmin_mode)
  , phase_saving     (opt_phase_saving)
  , rnd_pol          (false)
  , rnd_init_act     (opt_rnd_init_act)
  , garbage_frac     (opt_garbage_frac)
  , restart_first    (opt_restart_first)
  , restart_inc      (opt_restart_inc)

    // Parameters (the rest):
    //
  , learntsize_factor((double)1/(double)3), learntsize_inc(1.1)

    // Parameters (experimental):
    //
  , learntsize_adjust_start_confl (100)
  , learntsize_adjust_inc         (1.5)

  , addPropagationClauses			(opt_storing)
  , addConflictClauses				(opt_storing)
  , varOrderOptimization			(opt_inverting)
  , inactivePropagationOptimization	(opt_inactive)

    // Statistics: (formerly in 'SolverStats')
    //
  , solves(0), starts(0), decisions(0), rnd_decisions(0), propagations(0), conflicts(0)
  , dec_vars(0), num_clauses(0), num_learnts(0), clauses_literals(0), learnts_literals(0), max_literals(0), tot_literals(0)
  , sympropagations(0), symconflicts(0), invertingSyms(0)

  , watches            (WatcherDeleted(ca))
  , order_heap         (VarOrderLt(activity))

  , ok                 (true)
  , cla_inc            (1)
  , var_inc            (1)
  , qhead              (0)
  , simpDB_assigns     (-1)
  , simpDB_props       (0)
  , progress_estimate  (0)
  , remove_satisfied   (true)
  , next_var           (0)

    // Resource constraints:
    //
  , conflict_budget    (-1)
  , propagation_budget (-1)
  , asynch_interrupt   (false)
{}


Solver::~Solver()
{
}


//=================================================================================================
// Minor methods:


// Creates a new SAT variable in the solver. If 'decision' is cleared, variable will not be
// used as a decision variable (NOTE! This has effects on the meaning of a SATISFIABLE result).
//
Var Solver::newVar(lbool upol, bool dvar)
{
    Var v;
    if (free_vars.size() > 0){
        v = free_vars.last();
        free_vars.pop();
    }else
        v = next_var++;

    watches  .init(mkLit(v, false));
    watches  .init(mkLit(v, true ));
    assigns  .insert(v, l_Undef);
    vardata  .insert(v, mkVarData(CRef_Undef, 0));
    activity .insert(v, rnd_init_act ? drand(random_seed) * 0.00001 : 0);
    seen     .insert(v, 0);
    polarity .insert(v, true);
    user_pol .insert(v, upol);
    decision .reserve(v);
    trail    .capacity(v+1);
    setDecisionVar(v, dvar);
	decisionVars.push(false);
	watcherSymmetries.push();
	watcherSymmetries.push();
    return v;
}


// Note: at the moment, only unassigned variable will be released (this is to avoid duplicate
// releases of the same variable).
void Solver::releaseVar(Lit l)
{
    if (value(l) == l_Undef){
        addClause(l);
        released_vars.push(var(l));
    }
}


bool Solver::addClause_(vec<Lit>& ps)
{
    assert(decisionLevel() == 0);
    if (!ok) return false;

    // Check if clause is satisfied and remove false/duplicate literals:
    sort(ps);
    Lit p; int i, j;
    for (i = j = 0, p = lit_Undef; i < ps.size(); i++)
        if (value(ps[i]) == l_True || ps[i] == ~p)
            return true;
        else if (value(ps[i]) != l_False && ps[i] != p)
            ps[j++] = p = ps[i];
    ps.shrink(i - j);

    if (ps.size() == 0)
        return ok = false;
    else if (ps.size() == 1){
        uncheckedEnqueue(ps[0]);
        return ok = (propagate() == CRef_Undef);
    }else{
        CRef cr = ca.alloc(ps, false, false, false);
        clauses.push(cr);
        attachClause(cr);
    }

    return true;
}


void Solver::attachClause(CRef cr){
    const Clause& c = ca[cr];
    assert(c.size() > 1);
    watches[~c[0]].push(Watcher(cr, c[1]));
    watches[~c[1]].push(Watcher(cr, c[0]));
    if (c.learnt()) num_learnts++, learnts_literals += c.size();
    else            num_clauses++, clauses_literals += c.size();
}


void Solver::detachClause(CRef cr, bool strict){
    const Clause& c = ca[cr];
    assert(c.size() > 1);

    // Strict or lazy detaching:
    if (strict){
        remove(watches[~c[0]], Watcher(cr, c[1]));
        remove(watches[~c[1]], Watcher(cr, c[0]));
    }else{
        watches.smudge(~c[0]);
        watches.smudge(~c[1]);
    }

    if (c.learnt()) num_learnts--, learnts_literals -= c.size();
    else            num_clauses--, clauses_literals -= c.size();
}


void Solver::removeClause(CRef cr) {
    Clause& c = ca[cr];
    detachClause(cr);
    // Don't leave pointers to free'd memory!
    if (locked(c)) vardata[var(c[0])].reason = CRef_Undef;
    c.mark(1);
    ca.free(cr);
}


bool Solver::satisfied(const Clause& c) const {
    for (int i = 0; i < c.size(); i++)
        if (value(c[i]) == l_True)
            return true;
    return false; }


// Revert to the state at given level (keeping all assignment at 'level' but not beyond).
//
void Solver::cancelUntil(int level) {
    if (decisionLevel() > level){
    	if(verbosity>=2){ printf("Backtrack occurs on level %i to level %i\n",decisionLevel(),level); }

        for (int c = trail.size()-1; c >= trail_lim[level]; c--){
        	if(verbosity>=2){ printf("Back: %i\n",toDimacs(trail[c])); }

                notifySymmetriesBacktrack(trail[c]);
                decisionVars[var(trail[c])]=false;
                Var      x  = var(trail[c]);

                assigns [x] = l_Undef;

	    if (symmetry != nullptr)
                symmetry->updateCancel(trail[c]);
            if (phase_saving > 1 || (phase_saving == 1) && c > trail_lim.last())
                polarity[x] = sign(trail[c]);
            insertVarOrder(x);
        }

        qhead = trail_lim[level];
        trail.shrink(trail.size() - trail_lim[level]);
        trail_lim.shrink(trail_lim.size() - level);

    }


}

void Solver::addSymmetry(vec<Lit>& from, vec<Lit>& to){
	assert(from.size()==to.size());
	Symmetry* sym = new Symmetry(this, from, to, symmetries.size());
	bool isInverting = false;
	symmetries.push(sym);
	for(int i=0; i<from.size(); ++i){
		assert(from[i]!=to[i]);
		watcherSymmetries[toInt(from[i])].push(sym);

		if(from[i]==~to[i]){
			isInverting = true;
			if(varOrderOptimization){
				varBumpActivity(var(from[i]),-var_inc);
			}
		}
	}
	if(isInverting){
		++invertingSyms;
	}

	if(verbosity>=2){sym->print();}
	assert(testSymmetry(sym));
}

CRef Solver::propagateSymmetrical(Symmetry* sym, Lit l){
	assert(value(sym->getSymmetrical(l))!=l_True);
        bool isSymmetry = false;
        bool isFirstSymmetry = false;
        std::set<Symmetry*> comp;

	++sympropagations; //note: every symmetrical propagation either induces a conflict, or will be examined in the propagation queue (see the counter propagations)

	implic.clear();
	if(level(var(l))==0){
            assert(symmetry_units.find(var(l)) == symmetry_units.end());
            implic.push(sym->getSymmetrical(l));
            implic.push(~l);
	}else{
		assert(reason(var(l))!=CRef_Undef);
                const Clause& clause = ca[reason(var(l))];
                isSymmetry = clause.symmetry();
                isFirstSymmetry = clause.fsymmetry();
		sym->getSortedSymmetricalClause(ca[reason(var(l))], implic);
	}
	if(decisionLevel()>level(var(implic[1]))){
		cancelUntil(level(var(implic[1]))); // Backtrack verplicht om de watches op het juiste moment op de clause te zetten
	}
	assert(value(implic[0])!=l_True);
	assert(value(implic[1])==l_False);

        std::unique_ptr<std::set<Symmetry*>> compatibility = isSymmetry ?
            std::unique_ptr<std::set<Symmetry*>>(new std::set<Symmetry*>(ca[reason(var(l))].scompat()->begin(), ca[reason(var(l))].scompat()->end())) :
            nullptr;

	CRef cr = ca.alloc(implic, true, isFirstSymmetry, isSymmetry, std::move(compatibility));
	if(verbosity>=2){ printf("Symmetry clause added: "); testPrintClauseDimacs(cr); }
	if(value(implic[0])==l_Undef){
		assert( testPropagationClause(sym,l,implic) );
		if(addPropagationClauses){
			learnts.push(cr);
			attachClause(cr);
			claBumpActivity(ca[cr]);
		}
		uncheckedEnqueue(implic[0],cr);
		return CRef_Undef;
	}else{
		assert(value(implic[0])==l_False);
		assert( testConflictClause(sym,l,implic) );
		if(addConflictClauses){
			learnts.push(cr);
			attachClause(cr);
			claBumpActivity(ca[cr]);
		}
		++symconflicts;
		return cr;
	}
}


void Solver::notifySymmetriesBacktrack(Lit p) {
    CRef cr = reason(var(p));
    bool isBreakClause = cr != CRef_Undef && ca[cr].symmetry();
    Lit l;
    if (isBreakClause) {
        const Clause& clause = ca[cr];
        for (int j=0; j<clause.size(); j++) {
            l = clause[j];
            for(int i=watcherSymmetries[toInt(l)].size()-1; i>=0 ; --i) {
                watcherSymmetries[toInt(l)][i]->cancelReasonOfBreaked(p);
            }
        }
    }

    for(int i=watcherSymmetries[toInt(p)].size()-1; i>=0 ; --i){
        watcherSymmetries[toInt(p)][i]->notifyBacktrack(p);
    }

}

void Solver::notifySymmetries(Lit p){
	//	printf("Enqueueing %i at level %i - isDecision: %i\n",toInt(p),decisionLevel(),isDecision(p));

        Lit l;
        CRef cr = reason(var(p));
        bool isBreakClause = cr != CRef_Undef && ca[cr].symmetry();
        if (isBreakClause) {
            const Clause& clause = ca[cr];
            for (int j=0; j<clause.size(); j++) {
                l = clause[j];
                for(int i=watcherSymmetries[toInt(l)].size()-1; i>=0 ; --i) {
                    if (watcherSymmetries[toInt(l)][i]->isStab() &&
                        (*ca[cr].scompat()).find(watcherSymmetries[toInt(l)][i]) == (*ca[cr].scompat()).end())
                        watcherSymmetries[toInt(l)][i]->notifyReasonOfBreaked(p);
                }
            }

            // for (int i=symmetries.size()-1 ; i>=0; i--) {
            //     symmetries[i]->notifyBreakLit(p);
            // }
        }

        for(int i=watcherSymmetries[toInt(p)].size()-1; i>=0 ; --i){
            watcherSymmetries[toInt(p)][i]->notifyEnqueued(p);
	}

	assert( testActivityForSymmetries() );

}

// sym_testing

bool Solver::testSymmetry(Symmetry* sym){
	if(!debug){ return true; }
	for(int i=0; i<nClauses(); ++i){
		Clause& orig = ca[clauses[i]];
		std::set<Lit> orig_set;
		std::set<Lit> sym_set;
		for(int j=0; j<orig.size();++j){
			orig_set.insert(orig[j]);
			sym_set.insert(sym->getSymmetrical(orig[j]));
		}
		bool hasSymmetrical = sym_set==orig_set;
		for(int j=0; !hasSymmetrical && j<nClauses(); ++j){
			Clause& symmetrical=ca[clauses[j]];
			sym_set.clear();
			if(orig.size()==symmetrical.size()){
				for(int k=0; k<symmetrical.size(); ++k){
					sym_set.insert(sym->getInverse(symmetrical[k]));
				}
				hasSymmetrical = sym_set==orig_set;
			}
		}
		assert(hasSymmetrical);
	}
	return true;
}

bool Solver::testActivityForSymmetries(){
	if(!debug){ return true; }
	for(int i=0; i<symmetries.size(); ++i){
		if(symmetries[i]->isPermanentlyInactive()!=symmetries[i]->testIsPermanentlyInactive(trail) ){
			printf("ERROR: not sure if a symmetry is permanently inactive...\n");
			printf("symmetry says: %i - ",symmetries[i]->isPermanentlyInactive() );
			printf("test says: %i\n",symmetries[i]->testIsPermanentlyInactive(trail) );
			symmetries[i]->print();
			for(int j=0; j<trail.size(); ++j){
				printf("%i | %i | %i\n",level(var(trail[j])),toInt(trail[j]),isDecision(trail[j]));
			}
			assert(false);
		}
		if(symmetries[i]->isActive()!=symmetries[i]->testIsActive(trail) ){
			printf("ERROR: not sure if a symmetry is active...\n");
			printf("symmetry says: %i - ",symmetries[i]->isActive() );
			printf("test says: %i\n",symmetries[i]->testIsActive(trail) );
			symmetries[i]->print();
			for(int j=0; j<trail.size(); ++j){
				printf("%i | %i | %i\n",level(var(trail[j])),toInt(trail[j]),isDecision(trail[j]));
			}
			assert(false);
		}
	}
	return true;
}

void Solver::testPrintSymmetricalClauseInfo(Symmetry* sym, Lit l, vec<Lit>& reason){
	printf("Lit l: %i | sym(l): %i\n",toInt(l),toInt(sym->getSymmetrical(l)));
	sym->print();
	testPrintClause(reason);
}

void Solver::testPrintClause(vec<Lit>& reason){
	for(int i=0; i<reason.size(); ++i){
		printf("%i|", toInt(reason[i]));
		testPrintValue(reason[i]);
		printf("|%i ", level(var(reason[i])) );
	}printf("\n");
}

void Solver::testPrintClause(CRef clause){
	Clause& reason = ca[clause];
	for(int i=0; i<reason.size(); ++i){
		printf("%i|", toInt(reason[i]));
		testPrintValue(reason[i]);
		printf("|%i ", level(var(reason[i])) );
	}printf("\n");
}

void Solver::testPrintValue(Lit l){
	if(value(l)==l_False){
		printf("%c",'F');
	}else if(value(l)==l_True){
		printf("%c",'T');
	}else if(value(l)==l_Undef){
		printf("%c",'U');
	}else{
		printf("%c",'?');
	}
}

void Solver::testPrintClauseDimacs(CRef clause){
	Clause& reason = ca[clause];
	for(int i=0; i<reason.size(); ++i){
		printf("%i ", toDimacs(reason[i]));
	}printf("\n");
}

int Solver::toDimacs(Lit l){
	int result = var(l)+1;
	if(sign(l)){
		result *=-1;
	}
	return result;
}

bool Solver::testConflictClause(Symmetry* sym, Lit l, vec<Lit>& reasonn){
	if(!debug){ return true; }
	assert(reasonn.size()>1);
	assert(!isDecision(l));
	for(int i=1; i<reasonn.size(); ++i){
		if(level(var(reasonn[i]))>level(var(reasonn[0])) ){
			printf("ERROR: level of literal %i is higher than first literal of clause.\n",toInt(reasonn[i]));
			testPrintSymmetricalClauseInfo(sym,l,reasonn);
			testPrintTrail();
			assert(false);
		}
	}
	for(int i=2; i<reasonn.size(); ++i){
		if(level(var(reasonn[i]))>level(var(reasonn[1]))){
			printf("ERROR: level of literal %i is higher than second literal of clause.\n",toInt(reasonn[i]));
			testPrintSymmetricalClauseInfo(sym,l,reasonn);
			testPrintTrail();
			assert(false);
		}
	}
	for(int i=0; i<reasonn.size(); ++i){
		if(value(reasonn[i])!=l_False){
			printf("ERROR: value of literal %i is not false.\n",toInt(reasonn[i]));
			testPrintSymmetricalClauseInfo(sym,l,reasonn);
			testPrintTrail();
			assert(false);
		}
	}
	return true;
}

bool Solver::testPropagationClause(Symmetry* sym, Lit l, vec<Lit>& reasonn){
	if(!debug){ return true; }
	assert(reasonn.size()>1);
	assert(!isDecision(l));
	assert(value(reasonn[0])==l_Undef);
	for(int i=2; i<reasonn.size(); ++i){
		if(level(var(reasonn[i]))>level(var(reasonn[1]))){
			printf("ERROR: level of literal %i is higher than second literal of clause.\n",toInt(reasonn[i]));
			testPrintSymmetricalClauseInfo(sym,l,reasonn);
			testPrintTrail();
			assert(false);
		}
	}
	for(int i=1; i<reasonn.size(); ++i){
		if(value(reasonn[i])!=l_False){
			printf("ERROR: value of literal %i is not false.\n",toInt(reasonn[i]));
			testPrintSymmetricalClauseInfo(sym,l,reasonn);
			testPrintTrail();
			assert(false);
		}
	}
	return true;
}

void Solver::testPrintTrail(){
	for(int j=0; j<trail.size(); ++j){
		printf("%i|%i | %i\n",level(var(trail[j])),isDecision(trail[j]),toInt(trail[j]));
	}
}



//=================================================================================================
// Major methods:


Lit Solver::pickBranchLit()
{
    Var next = var_Undef;

    // Random decision:
    if (drand(random_seed) < random_var_freq && !order_heap.empty()){
        next = order_heap[irand(random_seed,order_heap.size())];
        if (value(next) == l_Undef && decision[next])
            rnd_decisions++; }

    // Activity based decision:
    while (next == var_Undef || value(next) != l_Undef || !decision[next])
        if (order_heap.empty()){
            next = var_Undef;
            break;
        }else
            next = order_heap.removeMin();

    // Choose polarity based on different polarity modes (global or per-variable):
    if (next == var_Undef)
        return lit_Undef;
    else if (user_pol[next] != l_Undef)
        return mkLit(next, user_pol[next] == l_True);
    else if (rnd_pol)
        return mkLit(next, drand(random_seed) < 0.5);
    else
        return mkLit(next, polarity[next]);
}


/*_________________________________________________________________________________________________
|
|  analyze : (confl : Clause*) (out_learnt : vec<Lit>&) (out_btlevel : int&)  ->  [void]
|
|  Description:
|    Analyze conflict and produce a reason clause.
|
|    Pre-conditions:
|      * 'out_learnt' is assumed to be cleared.
|      * Current decision level must be greater than root level.
|
|    Post-conditions:
|      * 'out_learnt[0]' is the asserting literal at level 'out_btlevel'.
|      * If out_learnt.size() > 1 then 'out_learnt[1]' has the greatest decision level of the
|        rest of literals. There may be others from the same level though.
|
|________________________________________________________________________________________________@*/
void Solver::analyze(CRef confl, vec<Lit>& out_learnt, int& out_btlevel, bool &out_symmetry, std::set<Symmetry*>* comp)
{
    int pathC = 0;
    Lit p     = lit_Undef;

    // Generate conflict clause:
    //
    out_learnt.push();      // (leave room for the asserting literal)
    int index   = trail.size() - 1;

    out_symmetry = false;
    bool fsym = ca[confl].fsymmetry();
    std::vector<CRef> conf_clauses;

    do{
        assert(confl != CRef_Undef); // (otherwise should be UIP)
        Clause& c = ca[confl];

        if (c.symmetry()) {
            out_symmetry = true;
            conf_clauses.push_back(confl);
        }

        if (c.learnt())
            claBumpActivity(c);

        for (int j = (p == lit_Undef) ? 0 : 1; j < c.size(); j++){
            Lit q = c[j];

            if (level(var(q)) == 0 && symmetry_units.find(var(q)) != symmetry_units.end())
                out_symmetry = true;

            if (!seen[var(q)] && level(var(q)) > 0){
                varBumpActivity(var(q));
                seen[var(q)] = 1;
                if (level(var(q)) >= decisionLevel())
                    pathC++;
                else
                    out_learnt.push(q);
            }

        }

        // Select next clause to look at:
        while (!seen[var(trail[index--])]);
        p     = trail[index+1];
        confl = reason(var(p));
        seen[var(p)] = 0;
        pathC--;

    }while (pathC > 0);
    out_learnt[0] = ~p;

    // Simplify conflict clause:

    int i, j;
    out_learnt.copyTo(analyze_toclear);
    if (ccmin_mode == 2){
        for (i = j = 1; i < out_learnt.size(); i++)
            if (reason(var(out_learnt[i])) == CRef_Undef || !litRedundant(out_learnt[i]))
                out_learnt[j++] = out_learnt[i];

    }else if (ccmin_mode == 1){
        for (i = j = 1; i < out_learnt.size(); i++){
            Var x = var(out_learnt[i]);

            if (reason(x) == CRef_Undef)
                out_learnt[j++] = out_learnt[i];
            else{
                Clause& c = ca[reason(var(out_learnt[i]))];
                for (int k = 1; k < c.size(); k++)
                    if (!seen[var(c[k])] && level(var(c[k])) > 0){
                        out_learnt[j++] = out_learnt[i];
                        break; }
            }
        }
    }else
        i = j = out_learnt.size();

    max_literals += out_learnt.size();
    out_learnt.shrink(i - j);
    tot_literals += out_learnt.size();
    //
    // Find correct backtrack level:
    //
    if (out_learnt.size() == 1)
        out_btlevel = 0;
    else{
        int max_i = 1;
        // Find the first literal assigned at the next-highest level:
        for (int i = 2; i < out_learnt.size(); i++)
            if (level(var(out_learnt[i])) > level(var(out_learnt[max_i])))
                max_i = i;
        // Swap-in this literal at index 1:
        Lit p             = out_learnt[max_i];
        out_learnt[max_i] = out_learnt[1];
        out_learnt[1]     = p;
        out_btlevel       = level(var(p));
    }

    for (int j = 0; j < analyze_toclear.size(); j++) seen[var(analyze_toclear[j])] = 0;    // ('seen[]' is now cleared)

    ////////////
    if (!out_symmetry)
        return;

    comp->clear();
    if (out_symmetry && !fsym) {
        for (CRef cr : conf_clauses) {
            std::set<Symmetry*>* check = ca[cr].scompat();
            if (check->empty()) {
                comp->clear();
                break;
            }

            if (comp->empty()) {
                comp->insert(check->begin(), check->end());
                continue;
            }

            // make intersection with c in place on comp
            std::set<Symmetry*>::iterator it1 = comp->begin();
            std::set<Symmetry*>::iterator it2 = check->begin();
            while ( (it1 != comp->end()) && (it2 != check->end()) ) {
                if (*it1 < *it2) {
                    comp->erase(it1++);
                } else if (*it2 < *it1) {
                    ++it2;
                } else { // *it1 == *it2
                    ++it1;
                    ++it2;
                }
            }
            comp->erase(it1, comp->end());

            if (comp->empty())
                break;
        }
    }

    for (int i=symmetries.size()-1; i>=0; --i) {
        Symmetry* sym = symmetries[i];
        if(comp->find(sym) != comp->end())
            continue;

        if (sym->stabilize(out_learnt))
            comp->insert(sym);
    }
}


// Check if 'p' can be removed from a conflict clause.
bool Solver::litRedundant(Lit p)
{
    enum { seen_undef = 0, seen_source = 1, seen_removable = 2, seen_failed = 3 };
    assert(seen[var(p)] == seen_undef || seen[var(p)] == seen_source);
    assert(reason(var(p)) != CRef_Undef);

    Clause*               c     = &ca[reason(var(p))];
    vec<ShrinkStackElem>& stack = analyze_stack;
    stack.clear();

    for (uint32_t i = 1; ; i++){
        if (i < (uint32_t)c->size()) {
            // Checking 'p'-parents 'l':
            Lit l = (*c)[i];

            // Variable at level 0 or previously removable:
            if (level(var(l)) == 0 || seen[var(l)] == seen_source || seen[var(l)] == seen_removable){
                continue; }

            // Check variable can not be removed for some local reason:
            if (reason(var(l)) == CRef_Undef || seen[var(l)] == seen_failed){
                stack.push(ShrinkStackElem(0, p));
                for (int i = 0; i < stack.size(); i++)
                    if (seen[var(stack[i].l)] == seen_undef){
                        seen[var(stack[i].l)] = seen_failed;
                        analyze_toclear.push(stack[i].l);
                    }

                return false;
            }

            // Recursively check 'l':
            stack.push(ShrinkStackElem(i, p));
            i  = 0;
            p  = l;
            c  = &ca[reason(var(p))];
        }else{
            // Finished with current element 'p' and reason 'c':
            if (seen[var(p)] == seen_undef){
                seen[var(p)] = seen_removable;
                analyze_toclear.push(p);
            }

            // Terminate with success if stack is empty:
            if (stack.size() == 0) break;

            // Continue with top element on stack:
            i  = stack.last().i;
            p  = stack.last().l;
            c  = &ca[reason(var(p))];

            stack.pop();
        }
    }

    return true;
}


/*_________________________________________________________________________________________________
|
|  analyzeFinal : (p : Lit)  ->  [void]
|
|  Description:
|    Specialized analysis procedure to express the final conflict in terms of assumptions.
|    Calculates the (possibly empty) set of assumptions that led to the assignment of 'p', and
|    stores the result in 'out_conflict'.
|________________________________________________________________________________________________@*/
void Solver::analyzeFinal(Lit p, vec<Lit>& out_conflict)
{
    out_conflict.clear();
    out_conflict.push(p);

    if (decisionLevel() == 0)
        return;

    seen[var(p)] = 1;

    for (int i = trail.size()-1; i >= trail_lim[0]; i--){
        Var x = var(trail[i]);
        if (seen[x]){
            if (reason(x) == CRef_Undef){
                assert(level(x) > 0);
                out_conflict.push(~trail[i]);
            }else{
                Clause& c = ca[reason(x)];
                for (int j = 1; j < c.size(); j++)
                    if (level(var(c[j])) > 0)
                        seen[var(c[j])] = 1;
            }
            seen[x] = 0;
        }
    }

    seen[var(p)] = 0;
}


void Solver::uncheckedEnqueue(Lit p, CRef from)
{
    assert(value(p) == l_Undef);
    assigns[var(p)] = lbool(!sign(p));
    vardata[var(p)] = mkVarData(from, decisionLevel());
    trail.push_(p);

    notifySymmetries(p);
}


/*_________________________________________________________________________________________________
|
|  propagate : [void]  ->  [Clause*]
|
|  Description:
|    Propagates all enqueued facts. If a conflict arises, the conflicting clause is returned,
|    otherwise CRef_Undef.
|
|    Post-conditions:
|      * the propagation queue is empty, even if there was a conflict.
|________________________________________________________________________________________________@*/
CRef Solver::propagate()
{
    CRef    confl     = CRef_Undef;
    int     num_props = 0;
    bool isSymmetryLevelZero;

    while (qhead < trail.size()){
        Lit            p   = trail[qhead++];     // 'p' is enqueued fact to propagate.
        vec<Watcher>&  ws  = watches.lookup(p);
        Watcher        *i, *j, *end;
        num_props++;

        if(verbosity>=2){ printf("Prop %i: %i\n",decisionLevel(),toDimacs(p)); }

        isSymmetryLevelZero = (decisionLevel() == 0 &&
                               symmetry_units.find(var(p)) != symmetry_units.end());

        // if (symmetry != nullptr && opt_esbp_begin) {
        //     symmetry->updateNotify(p);
        //     confl = learntSymmetryClause(cosy::ClauseInjector::ESBP, p);
        //     if (confl != CRef_Undef)
        //         return confl;
        // }
        // learntSymmetryClause(cosy::ClauseInjector::ESBP_FORCING, p);

        for (i = j = (Watcher*)ws, end = i + ws.size();  i != end;){
            // Try to avoid inspecting the clause:
            Lit blocker = i->blocker;
            if (value(blocker) == l_True){
                *j++ = *i++; continue; }

            // Make sure the false literal is data[1]:
            CRef     cr        = i->cref;
            Clause&  c         = ca[cr];
            Lit      false_lit = ~p;
            if (c[0] == false_lit)
                c[0] = c[1], c[1] = false_lit;
            assert(c[1] == false_lit);
            i++;

            // If 0th watch is true, then clause is already satisfied.
            Lit     first = c[0];
            Watcher w     = Watcher(cr, first);
            if (first != blocker && value(first) == l_True){
                *j++ = w; continue; }

            // Look for new watch:
            for (int k = 2; k < c.size(); k++)
                if (value(c[k]) != l_False){
                    c[1] = c[k]; c[k] = false_lit;
                    watches[~c[1]].push(w);
                    goto NextClause; }

            // Did not find watch -- clause is unit under assignment:
            *j++ = w;
            if (value(first) == l_False){
                confl = cr;
                qhead = trail.size();
                // Copy the remaining watches:
                while (i < end)
                    *j++ = *i++;
            }else {
                if (isSymmetryLevelZero)
                    symmetry_units.insert(var(first));

                uncheckedEnqueue(first, cr);
                // if (symmetry != nullptr && symmetry->hasClauseToInject(cosy::ClauseInjector::ESBP, first)) {
                //     while (i < end)
                //         *j++ = *i++;
                //     qhead = trail.size() - 1;
                // }
            }
        NextClause:;
        }
        ws.shrink(i - j);
        if (opt_esbp_end && symmetry != nullptr) {
            symmetry->updateNotify(p);
            learntSymmetryClause(cosy::ClauseInjector::ESBP, p);
        }

		// weakly active symmetry propagation: the condition qhead==trail.size() makes sure symmetry propagation is executed after unit propagation
		for( int i=symmetries.size()-1; qhead==trail.size() && confl==CRef_Undef && i>=0; --i){
			Symmetry* sym = symmetries[i];
			Lit orig = lit_Undef;
			if(sym->isActive()){
				orig = sym->getNextToPropagate();
				if(orig!=lit_Undef){
					confl = propagateSymmetrical(sym,orig);
				}
			}
		}

		// weakly inactive symmetry propagation: the condition qhead==trail.size() makes sure symmetry propagation is executed after unit propagation
		for( int i=symmetries.size()-1; inactivePropagationOptimization && qhead==trail.size() && confl==CRef_Undef && i>=0; --i){
			Symmetry* sym = symmetries[i];
                        if (!sym->isActive() && sym->isStab() && sym->isStabLevelZero()) {
				Lit orig = sym->getNextToPropagate();
				if(orig!=lit_Undef){
					confl = propagateSymmetrical(sym,orig);
				}
			}
		}

                // if (qhead == trail.size()) {
                //     CRef cr = learntSymmetryClause(cosy::ClauseInjector::ESBP);
                //     if (cr != CRef_Undef)
                //         confl = cr;
                // }

		if(confl!=CRef_Undef){
			qhead=trail.size();
		}

    }
    propagations += num_props;
    simpDB_props -= num_props;

    return confl;
}


/*_________________________________________________________________________________________________
|
|  reduceDB : ()  ->  [void]
|
|  Description:
|    Remove half of the learnt clauses, minus the clauses locked by the current assignment. Locked
|    clauses are clauses that are reason to some assignment. Binary clauses are never removed.
|________________________________________________________________________________________________@*/
struct reduceDB_lt {
    ClauseAllocator& ca;
    reduceDB_lt(ClauseAllocator& ca_) : ca(ca_) {}
    bool operator () (CRef x, CRef y) {
        return ca[x].size() > 2 && (ca[y].size() == 2 || ca[x].activity() < ca[y].activity()); }
};
void Solver::reduceDB()
{
    int     i, j;
    double  extra_lim = cla_inc / learnts.size();    // Remove any clause below this activity

    sort(learnts, reduceDB_lt(ca));
    // Don't delete binary or locked clauses. From the rest, delete clauses from the first half
    // and clauses with activity smaller than 'extra_lim':
    for (i = j = 0; i < learnts.size(); i++){
        Clause& c = ca[learnts[i]];
        if (c.size() > 2 && !locked(c) && (i < learnts.size() / 2 || c.activity() < extra_lim))
            removeClause(learnts[i]);
        else
            learnts[j++] = learnts[i];
    }
    learnts.shrink(i - j);
    checkGarbage();
}

// void Solver::removeSatisfied(vec<CRef>& cs)
// {
//     int i, j;
//     for (i = j = 0; i < cs.size(); i++){
//         Clause& c = ca[cs[i]];
//         if (satisfied(c))
//             removeClause(cs[i]);
//         else
//             cs[j++] = cs[i];
//     }
//     cs.shrink(i - j);
// }

// SPFS CODE
void Solver::removeSatisfied(vec<CRef>& cs)
{
    int i, j;
    for (i = j = 0; i < cs.size(); i++){
        Clause& c = ca[cs[i]];
        if (satisfied(c))
            removeClause(cs[i]);
        else{
            // Trim clause:
            // assert(value(c[0]) == l_Undef && value(c[1]) == l_Undef);
            for (int k = 2; k < c.size(); k++)
                if (value(c[k]) == l_False){
                    c[k--] = c[c.size()-1];
                    c.pop();
                }
            cs[j++] = cs[i];
        }
    }
    cs.shrink(i - j);
}


void Solver::rebuildOrderHeap()
{
    vec<Var> vs;
    for (Var v = 0; v < nVars(); v++)
        if (decision[v] && value(v) == l_Undef)
            vs.push(v);
    order_heap.build(vs);
}


/*_________________________________________________________________________________________________
|
|  simplify : [void]  ->  [bool]
|
|  Description:
|    Simplify the clause database according to the current top-level assigment. Currently, the only
|    thing done here is the removal of satisfied clauses, but more things can be put here.
|________________________________________________________________________________________________@*/
bool Solver::simplify()
{
    assert(decisionLevel() == 0);
    if (!ok || propagate() != CRef_Undef)
        return ok = false;

    if (nAssigns() == simpDB_assigns || (simpDB_props > 0))
        return true;

    // Remove satisfied clauses:
    removeSatisfied(learnts);
    if (remove_satisfied){       // Can be turned off.
        removeSatisfied(clauses);

        // TODO: what todo in if 'remove_satisfied' is false?

        // Remove all released variables from the trail:
        for (int i = 0; i < released_vars.size(); i++){
            assert(seen[released_vars[i]] == 0);
            seen[released_vars[i]] = 1;
        }

        int i, j;
        for (i = j = 0; i < trail.size(); i++)
            if (seen[var(trail[i])] == 0)
                trail[j++] = trail[i];
        trail.shrink(i - j);
        //printf("trail.size()= %d, qhead = %d\n", trail.size(), qhead);
        qhead = trail.size();

        for (int i = 0; i < released_vars.size(); i++)
            seen[released_vars[i]] = 0;

        // Released variables are now ready to be reused:
        append(released_vars, free_vars);
        released_vars.clear();
    }
    checkGarbage();
    rebuildOrderHeap();

    simpDB_assigns = nAssigns();
    simpDB_props   = clauses_literals + learnts_literals;   // (shouldn't depend on stats really, but it will do for now)

    return true;
}


/*_________________________________________________________________________________________________
|
|  search : (nof_conflicts : int) (params : const SearchParams&)  ->  [lbool]
|
|  Description:
|    Search for a model the specified number of conflicts.
|    NOTE! Use negative value for 'nof_conflicts' indicate infinity.
|
|  Output:
|    'l_True' if a partial assigment that is consistent with respect to the clauseset is found. If
|    all variables are decision variables, this means that the clause set is satisfiable. 'l_False'
|    if the clause set is unsatisfiable. 'l_Undef' if the bound on number of conflicts is reached.
|________________________________________________________________________________________________@*/
lbool Solver::search(int nof_conflicts)
{
    assert(ok);
    int         backtrack_level;
    // int current_level;
    bool        tag_symmetry;
    int         conflictC = 0;
    vec<Lit>    learnt_clause;
    starts++;

    for (;;){
        CRef confl = propagate();
        if (confl != CRef_Undef){
            // CONFLICT
            conflicts++; conflictC++;
            if (decisionLevel() == 0) return l_False;

            // current_level = decisionLevel();
            std::set<Symmetry*> comp;
            learnt_clause.clear();
            analyze(confl, learnt_clause, backtrack_level, tag_symmetry, &comp);
            cancelUntil(backtrack_level);


            if (learnt_clause.size() == 1){
                assert(decisionLevel() == 0);
                if (tag_symmetry)
                    symmetry_units.insert(var(learnt_clause[0]));

                uncheckedEnqueue(learnt_clause[0]);
            } else {
                bool first_symmetry = ca[confl].fsymmetry();
                assert(!first_symmetry || tag_symmetry);

                std::unique_ptr<std::set<Symmetry*>> compatibility = tag_symmetry ? std::unique_ptr<std::set<Symmetry*>>(new std::set<Symmetry*>(comp.begin(), comp.end())) : nullptr;
                CRef cr = ca.alloc(learnt_clause, true, first_symmetry, tag_symmetry, std::move(compatibility));
                // if (first_symmetry) {
                //     conflictC--;
                //     // setRandomPolarity(ca[cr]);
                // }

                learnts.push(cr);
                attachClause(cr);
                claBumpActivity(ca[cr]);
                uncheckedEnqueue(learnt_clause[0], cr);
                if(verbosity>=2){ printf("Conflict clause added: "); testPrintClauseDimacs(cr); }
            }

            varDecayActivity();
            claDecayActivity();

            if (--learntsize_adjust_cnt == 0){
                learntsize_adjust_confl *= learntsize_adjust_inc;
                learntsize_adjust_cnt    = (int)learntsize_adjust_confl;
                max_learnts             *= learntsize_inc;

                if (verbosity >= 1)
                    printf("| %9d | %7d %8d %8d | %8d %8d %6.0f | %6.3f %% |\n",
                           (int)conflicts,
                           (int)dec_vars - (trail_lim.size() == 0 ? trail.size() : trail_lim[0]), nClauses(), (int)clauses_literals,
                           (int)max_learnts, nLearnts(), (double)learnts_literals/nLearnts(), progressEstimate()*100);
            }

        }else{
            // NO CONFLICT
            if ((nof_conflicts >= 0 && conflictC >= nof_conflicts) || !withinBudget()){
                // Reached bound on number of conflicts:
                progress_estimate = progressEstimate();
                cancelUntil(0);
                return l_Undef; }

            // Simplify the set of problem clauses:
            if (decisionLevel() == 0 && !simplify())
                return l_False;

            if (learnts.size()-nAssigns() >= max_learnts)
                // Reduce the set of learnt clauses:
                reduceDB();

            Lit next = lit_Undef;
            while (decisionLevel() < assumptions.size()){
                // Perform user provided assumption:
                Lit p = assumptions[decisionLevel()];
                if (value(p) == l_True){
                    // Dummy decision level:
                    newDecisionLevel();
                }else if (value(p) == l_False){
                    analyzeFinal(~p, conflict);
                    return l_False;
                }else{
                    next = p;
                    break;
                }
            }

            if (next == lit_Undef){
                // New variable decision:
                decisions++;
                next = pickBranchLit();

                if (next == lit_Undef)
                    // Model found:
                    return l_True;
            }

            // Increase decision level and enqueue 'next'
            newDecisionLevel();
            decisionVars[var(next)]=true;
            uncheckedEnqueue(next);

            if ((uint64_t)decisionLevel() > max_decision_level)
                max_decision_level = decisionLevel();
        }
    }
}


double Solver::progressEstimate() const
{
    double  progress = 0;
    double  F = 1.0 / nVars();

    for (int i = 0; i <= decisionLevel(); i++){
        int beg = i == 0 ? 0 : trail_lim[i - 1];
        int end = i == decisionLevel() ? trail.size() : trail_lim[i];
        progress += pow(F, i) * (end - beg);
    }

    return progress / nVars();
}

/*
  Finite subsequences of the Luby-sequence:

  0: 1
  1: 1 1 2
  2: 1 1 2 1 1 2 4
  3: 1 1 2 1 1 2 4 1 1 2 1 1 2 4 8
  ...


 */

static double luby(double y, int x){

    // Find the finite subsequence that contains index 'x', and the
    // size of that subsequence:
    int size, seq;
    for (size = 1, seq = 0; size < x+1; seq++, size = 2*size+1);

    while (size-1 != x){
        size = (size-1)>>1;
        seq--;
        x = x % size;
    }

    return pow(y, seq);
}

// NOTE: assumptions passed in member-variable 'assumptions'.
lbool Solver::solve_()
{
    model.clear();
    conflict.clear();
    if (!ok) return l_False;

    // Set symmetry order
    if (symmetry != nullptr) {
        symmetry->enableCosy(cosy::OrderMode::AUTO,
                             cosy::ValueMode::TRUE_LESS_FALSE);
        symmetry->printInfo();
    }

    notifyCNFUnits();

    if (symmetry != nullptr) {
        cosy::ClauseInjector::Type type = cosy::ClauseInjector::UNITS;
	while (symmetry->hasClauseToInject(type)) {
            std::vector<Lit> literals = symmetry->clauseToInject(type);
            assert(literals.size() == 1);
            Lit l = literals[0];
            symmetry_units.insert(var(l));
	    uncheckedEnqueue(l);
	}
    }
    solves++;

    max_learnts               = nClauses() * learntsize_factor;
    learntsize_adjust_confl   = learntsize_adjust_start_confl;
    learntsize_adjust_cnt     = (int)learntsize_adjust_confl;
    lbool   status            = l_Undef;

    if (verbosity >= 1){
        printf("============================[ Search Statistics ]==============================\n");
        printf("| Conflicts |          ORIGINAL         |          LEARNT          | Progress |\n");
        printf("|           |    Vars  Clauses Literals |    Limit  Clauses Lit/Cl |          |\n");
        printf("===============================================================================\n");
    }

    // Search:
    int curr_restarts = 0;
    while (status == l_Undef){
        double rest_base = luby_restart ? luby(restart_inc, curr_restarts) : pow(restart_inc, curr_restarts);
        status = search(rest_base * restart_first);

        // // Dynamic Order
        // if (status == l_Undef) {
        //     if (symmetry != nullptr) {
        //         symmetry->enableCosy(getVSIDSVector(),
        //                              cosy::ValueMode::TRUE_LESS_FALSE);
        //     }

        //     cleanAllSymmetricClauses();

        //     if (symmetry != nullptr) {
        //         cosy::ClauseInjector::Type type = cosy::ClauseInjector::UNITS;
        //         while (symmetry->hasClauseToInject(type)) {
        //             std::vector<Lit> literals = symmetry->clauseToInject(type);
        //             assert(literals.size() == 1);
        //             Lit l = literals[0];
        //             if (value(l) == l_Undef) {
        //                 symmetry_units.insert(var(l));
        //                 uncheckedEnqueue(l);
        //             }
        //         }
        //     }

        // }

        // assert(decisionLevel() == 0);
        // std::cout << "units:";
        // for (auto v : symmetry_units)
        //     std::cout << v << " ";
        // std::cout << std::endl;
        // std::cout << "trail:";
        // for (int i=0; i<trail.size(); i++)
        //     std::cout << var(trail[i]) << " ";
        // std::cout << std::endl;

        if (!withinBudget()) break;
        curr_restarts++;
    }

    if (verbosity >= 1)
        printf("===============================================================================\n");


    if (status == l_True){
        // Extend & copy model:
        model.growTo(nVars());
        for (int i = 0; i < nVars(); i++) model[i] = value(i);
    }else if (status == l_False && conflict.size() == 0)
        ok = false;

    cancelUntil(0);
    return status;
}


bool Solver::implies(const vec<Lit>& assumps, vec<Lit>& out)
{
    trail_lim.push(trail.size());
    for (int i = 0; i < assumps.size(); i++){
        Lit a = assumps[i];

        if (value(a) == l_False){
            cancelUntil(0);
            return false;
        }else if (value(a) == l_Undef)
            uncheckedEnqueue(a);
    }

    unsigned trail_before = trail.size();
    bool     ret          = true;
    if (propagate() == CRef_Undef){
        out.clear();
        for (int j = trail_before; j < trail.size(); j++)
            out.push(trail[j]);
    }else
        ret = false;

    cancelUntil(0);
    return ret;
}

//=================================================================================================
// Writing CNF to DIMACS:
//
// FIXME: this needs to be rewritten completely.

static Var mapVar(Var x, vec<Var>& map, Var& max)
{
    if (map.size() <= x || map[x] == -1){
        map.growTo(x+1, -1);
        map[x] = max++;
    }
    return map[x];
}


void Solver::toDimacs(FILE* f, Clause& c, vec<Var>& map, Var& max)
{
    if (satisfied(c)) return;

    for (int i = 0; i < c.size(); i++)
        if (value(c[i]) != l_False)
            fprintf(f, "%s%d ", sign(c[i]) ? "-" : "", mapVar(var(c[i]), map, max)+1);
    fprintf(f, "0\n");
}


void Solver::toDimacs(const char *file, const vec<Lit>& assumps)
{
    FILE* f = fopen(file, "wr");
    if (f == NULL)
        fprintf(stderr, "could not open file %s\n", file), exit(1);
    toDimacs(f, assumps);
    fclose(f);
}


void Solver::toDimacs(FILE* f, const vec<Lit>& assumps)
{
    // Handle case when solver is in contradictory state:
    if (!ok){
        fprintf(f, "p cnf 1 2\n1 0\n-1 0\n");
        return; }

    vec<Var> map; Var max = 0;

    // Cannot use removeClauses here because it is not safe
    // to deallocate them at this point. Could be improved.
    int cnt = 0;
    for (int i = 0; i < clauses.size(); i++)
        if (!satisfied(ca[clauses[i]]))
            cnt++;

    for (int i = 0; i < clauses.size(); i++)
        if (!satisfied(ca[clauses[i]])){
            Clause& c = ca[clauses[i]];
            for (int j = 0; j < c.size(); j++)
                if (value(c[j]) != l_False)
                    mapVar(var(c[j]), map, max);
        }

    // Assumptions are added as unit clauses:
    cnt += assumps.size();

    fprintf(f, "p cnf %d %d\n", max, cnt);

    for (int i = 0; i < assumps.size(); i++){
        assert(value(assumps[i]) != l_False);
        fprintf(f, "%s%d 0\n", sign(assumps[i]) ? "-" : "", mapVar(var(assumps[i]), map, max)+1);
    }

    for (int i = 0; i < clauses.size(); i++)
        toDimacs(f, ca[clauses[i]], map, max);

    if (verbosity > 0)
        printf("Wrote DIMACS with %d variables and %d clauses.\n", max, cnt);
}


void Solver::printStats() const
{
    double cpu_time = cpuTime();
    double mem_used = memUsedPeak();
    printf("restarts              : %" PRIu64"\n", starts);
    printf("conflicts             : %-12" PRIu64"   (%.0f /sec)\n", conflicts   , conflicts   /cpu_time);
    printf("symconflicts          : %-12" PRIu64"   (%.0f /sec)\n", symconflicts   , symconflicts   /cpu_time);
    printf("decisions             : %-12" PRIu64"   (%4.2f %% random) (%.0f /sec)\n", decisions, (float)rnd_decisions*100 / (float)decisions, decisions   /cpu_time);
    printf("propagations          : %-12" PRIu64"   (%.0f /sec)\n", propagations, propagations/cpu_time);
    printf("sympropagations       : %-12" PRIu64"   (%.0f /sec)\n", sympropagations, sympropagations/cpu_time);
    printf("conflict literals     : %-12" PRIu64"   (%4.2f %% deleted)\n", tot_literals, (max_literals - tot_literals)*100 / (double)max_literals);
    if (mem_used != 0) printf("Memory used           : %.2f MB\n", mem_used);

    printf("max decision level    : %" PRIu64"\n", max_decision_level);

    printf("CPU time              : %g s\n", cpu_time);
    if (symmetry != nullptr)
        symmetry->printStats();

    // _stats.print();
}


//=================================================================================================
// Garbage Collection methods:

void Solver::relocAll(ClauseAllocator& to)
{
    // All watchers:
    //
    watches.cleanAll();
    for (int v = 0; v < nVars(); v++)
        for (int s = 0; s < 2; s++){
            Lit p = mkLit(v, s);
            vec<Watcher>& ws = watches[p];
            for (int j = 0; j < ws.size(); j++)
                ca.reloc(ws[j].cref, to);
        }

    // All reasons:
    //
    for (int i = 0; i < trail.size(); i++){
        Var v = var(trail[i]);

        // Note: it is not safe to call 'locked()' on a relocated clause. This is why we keep
        // 'dangling' reasons here. It is safe and does not hurt.
        if (reason(v) != CRef_Undef && (ca[reason(v)].reloced() || locked(ca[reason(v)]))){
            assert(!isRemoved(reason(v)));
            ca.reloc(vardata[v].reason, to);
        }
    }

    // All learnt:
    //
    int i, j;
    for (i = j = 0; i < learnts.size(); i++)
        if (!isRemoved(learnts[i])){
            ca.reloc(learnts[i], to);
            learnts[j++] = learnts[i];
        }
    learnts.shrink(i - j);

    // All original:
    //
    for (i = j = 0; i < clauses.size(); i++)
        if (!isRemoved(clauses[i])){
            ca.reloc(clauses[i], to);
            clauses[j++] = clauses[i];
        }
    clauses.shrink(i - j);
}


void Solver::garbageCollect()
{
    // Initialize the next region to a size corresponding to the estimated utilization degree. This
    // is not precise but should avoid some unnecessary reallocations for the new region:
    ClauseAllocator to(ca.size() - ca.wasted());

    relocAll(to);
    if (verbosity >= 2)
        printf("|  Garbage collection:   %12d bytes => %12d bytes             |\n",
               ca.size()*ClauseAllocator::Unit_Size, to.size()*ClauseAllocator::Unit_Size);
    to.moveTo(ca);
}

CRef Solver::learntSymmetryClause(cosy::ClauseInjector::Type type, Lit p) {
    if (symmetry != nullptr) {
        if (symmetry->hasClauseToInject(type, p)) {
            std::vector<Lit> vsbp = symmetry->clauseToInject(type, p);

            // Dirty make a copy of vector
            vec<Lit> sbp;
            for (Lit l : vsbp) {
                assert(value(l) == l_False);
                sbp.push(l);
            }

            std::set<Symmetry*>comp;
            for( int i=symmetries.size()-1; i>=0; --i){
                Symmetry* sym = symmetries[i];
                if(sym->stabilize(sbp)){
                    comp.insert (sym);
                }
	    }
            std::unique_ptr<std::set<Symmetry*>> compatibility(new std::set<Symmetry*>(comp.begin(), comp.end()));
            CRef cr = ca.alloc(sbp, true, true, true, std::move(compatibility));
            learnts.push(cr);
            attachClause(cr);
            // _stats.sizeESBP.add(sbp.size());
            // _stats.decisionLevelESBP.add(decisionLevel());

            return cr;
        }
    }
    return CRef_Undef;
}

// Generate Clause without reason
CRef Solver::learntSymmetryClause(cosy::ClauseInjector::Type type) {
    if (symmetry != nullptr) {
        if (symmetry->hasClauseToInject(type)) {
            std::vector<Lit> vsbp = symmetry->clauseToInject(type);

            // Dirty make a copy of vector
            vec<Lit> sbp;
            for (Lit l : vsbp) {
                sbp.push(l);
                assert(value(l) == l_False);
            }

            CRef cr = ca.alloc(sbp, true, true, true);

            // setRandomPolarity(ca[cr]);
            // learnts.push(cr);
            attachClause(cr);
            return cr;
        }
    }

    return CRef_Undef;
}


void Solver::cleanAllSymmetricClauses() {
    assert(decisionLevel() == 0);

    int i, j;
    // Manage clauses
    for (i = j = 0; i < learnts.size(); i++){
        Clause& c = ca[learnts[i]];
        if (c.symmetry())
            removeClause(learnts[i]);
        else
            learnts[j++] = learnts[i];
    }
    learnts.shrink(i - j);
    checkGarbage();


    // Manage Units
    std::vector<Lit> real_units;
    for (i = 0; i < trail.size(); i++) {
        Lit l = trail[i];
        Var x = var(l);
        if (symmetry_units.find(x) == symmetry_units.end())
            real_units.push_back(l);
    }

    // Cancel Level 0
    for (i = trail.size() -1; i >= 0; i--) {
        Lit l = trail[i];
        Var x = var(l);

        notifySymmetriesBacktrack(l);
        decisionVars[x] = false;
        assigns [x] = l_Undef;

        if (symmetry != nullptr)
            symmetry->updateCancel(l);
        insertVarOrder(x);
    }

    rebuildOrderHeap();

    for (i = 0; i<symmetries.size(); i++)
        symmetries[i]->resetBreakUnits();

    qhead = 0;
    trail.clear();
    trail_lim.clear();
    symmetry_units.clear();

    for (const Lit & l : real_units)
        uncheckedEnqueue(l);


    // std::cout << "SYM UNITS : ";
    // for (const Var v : symmetry_units) {
    //     std::cout << v << " ";
    // }
    // std::cout << std::endl;

    // std::cout << "TRAIL     : ";
    // for (i = j = 0; i<trail.size(); i++)
    //     std::cout << var(trail[i]) << " ";
    // std::cout << std::endl;

}


void Solver::notifyCNFUnits() {
    assert(decisionLevel() == 0);

    for (int i=0; i<trail.size(); i++) {
        notifySymmetries(trail[i]);

        if (symmetry != nullptr)
            symmetry->updateNotify(trail[i]);
    }
}

std::vector<Lit> Solver::getVSIDSVector() {
    std::vector<Lit> vsids_order;

    vec<Var> vars;
    for (int i=0; i<nVars(); i++)
        vars.push(i);

    sort(vars, VarOrderLt(activity));

    for (int i=0; i<nVars(); i++)
        vsids_order.push_back(mkLit(vars[i], false));

    return vsids_order;
}

void Solver::setRandomPolarity(const Clause& clause) {
    int sz = clause.size();
    for (int i=0; i<sz; i++)
        polarity[var(clause[i])] = rand() % 2;
}

void Solver::sortESBP(vec<Lit>& out_clause) {
    int first=0;
    int second=1;
    for(int i=0; i<out_clause.size(); ++i){
        assert(value(out_clause[i])!=l_True);
        if(		 value(out_clause[first])!=l_Undef &&
                         (value(out_clause[i])==l_Undef || hasLowerLevel(out_clause[first],out_clause[i])) ){
            second = first; first=i;
        }else if(value(out_clause[second])!=l_Undef &&
                 (value(out_clause[i])==l_Undef || hasLowerLevel(out_clause[second],out_clause[i])) ){
            second = i;
        }
    }

    // note: swapping the final literals to their place is pretty tricky. Imagine for instance the case where first==0 or second==1 ;)
    if(first!=0){
        Lit temp = out_clause[0]; out_clause[0]=out_clause[first]; out_clause[first]=temp;
    }
    assert(second!=first);
    if(second==0){ second=first; }
    if(second!=1){
        Lit temp = out_clause[1];
        out_clause[1]=out_clause[second];
        out_clause[second]=temp;
    }
}
