// {{{ GPL License

// This file is part of libcsp - a library for handling linear constraints.
// Copyright (C) 2016  Max Ostrowski

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

// }}}

#include <clingcon/appsupport.h>
#include <program_opts/program_options.h>
#include <program_opts/typed_value.h>


namespace clingcon
{

Helper::Helper(Clasp::SharedContext& ctx, Clasp::Cli::ClaspCliConfig& claspConfig, Clasp::Asp::LogicProgram* lp, order::Config& conf) : ctx_(ctx), td_(lp->theoryData()),
                                                                          lp_(lp), mctx_(ctx), n_(new order::Normalizer(mctx_,conf)),
                                                                          conf_(conf), configurator_(conf_,*n_.get(),to_),
                                                                          tp_(*n_.get(),td_,lp,mctx_.trueLit())
{
    claspConfig.addConfigurator(&configurator_,Clasp::Ownership_t::Type::Retain, false);

}

void Helper::addOptions(ProgramOptions::OptionContext& root, order::Config& conf)
{
    ProgramOptions::OptionGroup cspconf("Constraint Processing Options", ProgramOptions::desc_level_e1);
    cspconf.addOptions()
            ("redundant-nogood-check", ProgramOptions::storeTo(conf.redundantClauseCheck = true), "Check translated nogoods for redundancies (default: true)")
            ("domain-size", ProgramOptions::storeTo(conf.domSize = 10000), "Restrict the number of ranges a domain can have when multiplied (-1=all) (default: 10000)")
            ("break-symmetries", ProgramOptions::storeTo(conf.break_symmetries = true), "Break symmetries (necessary for enumeration) (default: true)")
            ("split-size", ProgramOptions::storeTo(conf.splitsize_maxClauseSize.first = 3)->arg("<n>"), "Split constraints into size %A (minimum: 3, -1=all) (default: 3)")
            ("max-nogoods-size", ProgramOptions::storeTo(conf.splitsize_maxClauseSize.second = 1024)->arg("<n>"), "Constraints are only split if they would produce more then %A nogoods (default: 1024)")
            ("pidgeon-optimization", ProgramOptions::storeTo(conf.pidgeon = true), "Add pidgeon-hole constraints for distinct (default: true)")
            ("permutation-optimization", ProgramOptions::storeTo(conf.permutation = false), "Add permutation constraints for distinct (default: false)")
            ("distinct-to-card", ProgramOptions::storeTo(conf.alldistinctCard = true), "Translate distinct constraint using cardinality constraints (default: true)")
            ("explicit-binary-order", ProgramOptions::storeTo(conf.explicitBinaryOrderClausesIfPossible = false), "Create binary order nogoods if possible (default: false)")
            ("learn-nogoods", ProgramOptions::storeTo(conf.learnClauses = true), "Learn nogoods while propagating (default: true)")
            ("translate-constraints", ProgramOptions::storeTo(conf.translateConstraints = 1000)->arg("<n>"), "Translate constraints with an estimated number of nogoods less than %A (-1=all) (default: 1000)")
            ("min-lits-per-var", ProgramOptions::storeTo(conf.minLitsPerVar = 1000)->arg("<n>"), "Creates at least %A literals per variable (-1=all) (default: 1000)")
            ("equality-processing", ProgramOptions::storeTo(conf.equalityProcessing = true), "Replace equal variable views (default: true)")
            ("flatten-optimization", ProgramOptions::storeTo(conf.optimizeOptimize = false), "Flatten the optimization statement (default: false)")
            ("sort-coefficient", ProgramOptions::storeTo(conf.coefFirst = false), "Sort constraints by coefficient first (otherwise domain size) (default: false)")
            ("sort-descend-coef", ProgramOptions::storeTo(conf.descendCoef = true), "Sort constraints by descending coefficients (otherwise ascending) (default: true)")
            ("sort-descend-dom", ProgramOptions::storeTo(conf.descendDom = false), "Sort constraints by descending domain size (otherwise ascending) (default: false)")
            ("prop-strength", ProgramOptions::storeTo(conf.propStrength = 4)->arg("<n>"), "Propagation strength %A {1=weak .. 4=strong} (default: 4)")
            ("sort-queue", ProgramOptions::storeTo(conf.sortQueue = false), "Sort lazy propagation queue by constraint size (default: false)")
            ("convert-lazy-variables", ProgramOptions::storeTo(conf.convertLazy = std::make_pair(0,false))->arg("<n,b>"), "Add the union(b=true)/intersection(b=false) of the lazy variables of the first n threads (default: 0,false)")
            ("strict", ProgramOptions::storeTo(conf.strict = false), "Enable strict (redundant) constraints (default: false)")
            ;
    root.add(cspconf);

    ProgramOptions::OptionGroup hidden("Constraint Processing Options 2",ProgramOptions::desc_level_hidden);
    cspconf.addOptions()
            //("difference-logic", ProgramOptions::storeTo(conf.dlprop = 0), "0: no difference logic propagator, 1 early, 2 late  (default: 0)")
            ("disjoint-to-distinct", ProgramOptions::storeTo(conf.disjoint2distinct = false), "Translate disjoint to distinct constraint if possible (default: false)")
            ;
    root.add(hidden);
}

void Helper::transformHeadConstraints(Clasp::Asp::PrgAtom* a)
{
    ///1. Transform all rules where we have a theory atom in the head
    ///1.1 ta :- B   nach :- B, not ta.
    ///     therefore, get PrgBody, call remove head, create new body with all
    /// goals (literals) of the old body plus not ta.
    ///1.2 ta :- lb {B}. nach :- sum(B)+1 {B, not ta=sum(B)+1-lb}
    ///1.3 ta v tb v c :- B nach c :- B, not ta, not tb.
    ///Missing: Facts ta, and empty body
    Clasp::Asp::EdgeVec supps;
    a->clearSupports(supps);/// get all bodies and remove them (does this also remove the other direction? (body -> head ?))
    /// for each rule where a is in the head, we need to move it into the body
    for (auto s : supps)
    {
        if (s.isBody())
        {
            Clasp::Asp::PrgBody* prgbody = lp_->getBody(s.node());
            prgbody->removeHead(a, s.type());
            if (prgbody->type()==Clasp::Asp::Body_t::Normal)
            {
                auto rb = lp_->startRule();
                for (uint32 id = 0; id < prgbody->size(); ++id)
                {
                    rb.addToBody(prgbody->goal(id).var(),!prgbody->goal(id).sign());
                }
                rb.addToBody(a->id(),false);
                rb.endRule();
            }
            else
            {
                auto rb = lp_->startWeightRule(prgbody->sumW()+1);
                for (uint32 id = 0; id < prgbody->size(); ++id)
                {
                    rb.addToBody(prgbody->goal(id).var(),!prgbody->goal(id).sign(),prgbody->weight(id));
                }
                rb.addToBody(a->id(), false, prgbody->sumW()+1 - prgbody->bound());
                rb.endRule();
            }
        }
        else if (s.isDisj())
        {
            /// wait for benni's response on how to identify theory atoms
            throw std::runtime_error("theory atoms in disjunctions not supported");
//            Clasp::Asp::PrgDisj* disj = lp_->getDisj(s.node());
//            Clasp::VarVec theoryAtoms;
//            Clasp::VarVec rest;
//            for (auto i : disj)
//            {
//                //if ()
//                td_.getElement()
//            }
//                // remove disjunction D from program -
//                // also removes edge between B and D
//                D->detach(*lp_);

        }
        /// else ?
    }
}


void Helper::postRead()
{
    tdinfo_.clear();
    std::unordered_map<Potassco::Id_t,bool> atoms; // i know i know
    for (auto i = td_.currBegin(); i != td_.end(); ++i)
    {
        auto atom = (*i)->atom();
        bool isClingcon = tp_.isClingconConstraint(i);
        atom = lp_->getAtom(atom)->id();
        assert(lp_->validAtom(atom));
        assert(lp_->getAtom(atom)->relevant());
        Clasp::Asp::PrgAtom* a = lp_->getAtom(atom);
        order::Direction info = order::Direction::NONE;
        if (isClingcon)
        {
            transformHeadConstraints(a);
            //a = lp_->getAtom(atom);


            /// Missing: Completion for bodies which are aggregates ?
            /// Reassure: I do add newAtoms for the manual completion,
            /// how does this work in the incremental setting, that GRINGO
            /// does not reuse these numbers !?!?!?!?
            //DO IT


            std::vector<Clasp::LitVec> completion;

            //std::cout << "level: " << ctx_.solver(0)->level(lit.var()) << " ";
            //std::cout << "isTrue: " <<  a->value()==Clasp::value_true << " ";
            //std::cout << "isFalse: " <<  a->value()==Clasp::value_false << " " << std::endl;

            //info.value=a->value();
            //std::cout << "sign: " <<  lit.sign() << " " << std::endl;
            if (a->value()==Clasp::value_true)
            {
                info |= order::Direction::FWD;
            }
            else
            if (a->value()==Clasp::value_false)
            {
                info |= order::Direction::BACK;
            }
            else if (!conf_.strict)
            {

                /// can an atom occur true/false in a body ? I think not, so this is a check for body/integrity constraint
                for (Clasp::Asp::PrgAtom::dep_iterator it = a->deps_begin(), end = a->deps_end(); it != end; ++it) {
                  uint32 bodyId = it->var();
                  const Clasp::Asp::PrgBody* b = lp_->getBody(bodyId);
                  if (b->eq()) {
                    /// can i savely ignore this, as it also occurs in the other body ?
                    continue;
                    //b = prg.getBody(bodyId = prg.getEqBody(bodyId));
                    //printf("Atom %u occurs in body %u, which is eq to body %u\n", atom, it->var(), bodyId);
                  }
                  if (b->relevant()) {
                    //printf("Atom %u occurs in b%c of body with id %u = {", atom, it->sign() ? '-' : '+', bodyId);
                    if (b->value()==Clasp::value_false)
                    {
                        /// we have an integrity constraint
                        if (it->sign())
                            info |= order::Direction::FWD;
                        else
                            info |= order::Direction::BACK;
                    }
                    else
                    {
                        info = order::Direction::EQ;
                    }
                    if (info!= order::Direction::EQ)
                    {
                        completion.push_back(Clasp::LitVec());
                        for (auto elem = b->goals_begin(); elem != b->goals_end(); ++elem)
                        {
                            if (lp_->getAtom(elem->var())->id()!=a->id())
                                completion.back().push_back(*elem);
                        }
                    }
                    // Do something with body, e.g. iterate over its elements
                    // via goals_begin()/goals_end();
                  }
                }

                /// special case, can't occur with gringo, but with other translations
                /// does not work in multi-shot, due to "completion"
                if (atoms.find(atom) == atoms.end()) /// not yet found
                {
                    atoms[atom]=true;
                }else
                {
                    /// already found
                    info = order::Direction::EQ;
                    // this theory atom maybe does not occur in any rule,
                    // it is simply equivalent to an atom which occurs somewhere
                    lp_->startChoiceRule().addHead((*i)->atom()).endRule();// warning, this makes it a defined atom
                }
            }

            if (lp_->isDefined(atom))
                info |= order::Direction::FWD;

            //if (atom!=0 && isClingcon && (conf_.strict || occursInBody(*lp_,(*i)->atom())))
            //    lp_->startChoiceRule().addHead((*i)->atom()).endRule();
            if (conf_.strict)
                info = order::Direction::EQ;

            if (tp_.isUnarySum(i))
                info = order::Direction::EQ;

            if (a->value()==Clasp::value_free && (info == order::Direction::FWD || info == order::Direction::BACK))
            {
                bool fwd = (info == order::Direction::FWD);
                Clasp::LitVec newLits;
                //        COMPLETION MACHEN,
                //                für alle die FWD sind, alle Teilbodies aus allen integ constr. müssen falsch sein, dann kann das constraint atom falsch gemacht werden
                //                :- b1,b2,b3,b4, not "x > 7".
                //                :- c1,c2,c3,c4, not "x > 7".
                for (auto& body : completion)
                {
                    if (body.size()>1)
                    {
                        newLits.push_back(Clasp::Literal(lp_->newAtom(),false));
                        for (auto& lit : body)
                        {
                            // aux means that the body is false
                            //                aux(b) :- not b1.
                            //                aux(b) :- not b2.
                            //                aux(b) :- not b3.
                            //                aux(c) :- not c1.
                            //                aux(c) :- not c2.
                            //                aux(c) :- not c3.
                            lp_->startRule().addHead(newLits.back().var()).addToBody(lit.var(),lit.sign()).endRule();
                        }
                    }
                    else
                    {
                        assert(body.size()==1);
                        newLits.push_back(~body.back());
                    }
                }

                auto rule = lp_->startRule();
                assert(newLits.size());
                for (auto i : newLits)
                {
                    //                :- aux(b), aux(c), "x > 7".
                    rule.addToBody(i.var(),!i.sign());
                }
                rule.addToBody(atom,fwd).endRule();
            }

        }
        if (info==order::Direction::NONE)
            info=order::Direction::EQ; /// special case,  can't occur with gringo, but with manually created files (flatzinc)

        tdinfo_.emplace_back(info);
    }
}


bool Helper::occursInBody(Clasp::Asp::LogicProgram& p, Potassco::Atom_t aId)
{
    if (!aId || !p.validAtom(aId)) { return false; }
    Clasp::Asp::PrgAtom* a = p.getRootAtom(aId);
    return a->hasDep(Clasp::Asp::PrgAtom::dep_all);
} 


bool Helper::postEnd()
{
    if (lp_->end() && ctx_.master()->propagate())
    {
        bool conflict = false;
        conflict = !ctx_.master()->propagate();
        if (!conflict)
        {

            int count = 0;
            for (auto i = td_.currBegin(); i != td_.end(); ++i)
            {
                assert((!tp_.isClingconConstraint(i)) || tdinfo_[count]!=order::Direction::NONE);
                tp_.readConstraint(i, tdinfo_[count++]);
            }
            to_.names_ = tp_.postProcess();
            ctx_.output.theory = &to_;
            simplifyMinimize();
            conflict = !n_->prepare();
        }


        if (!conflict)
        {
            do
            {
                conflict = !ctx_.master()->propagate();
                if (!conflict)
                    conflict = !n_->propagate();
            }while(!conflict && !n_->atFixPoint());
        }

        if (!conflict)
            conflict = !n_->finalize();

        if (conflict && !ctx_.master()->hasConflict())
            ctx_.master()->force(Clasp::Literal(0,true));
     }
     tp_.reset();
     return true;
}

void Helper::postSolve()
{
    std::vector<const order::VolatileVariableStorage*> vvs;
    for (unsigned int thread = 0; thread < std::min(conf_.convertLazy.first,64u); ++thread)
    {
        if (to_.props_[thread]==nullptr)
            break;
        vvs.emplace_back(&to_.props_[thread]->getVVS());
    }

    n_->convertAuxLiterals(vvs, ctx_.numVars());
}


void Helper::simplifyMinimize()
{
    for (unsigned int level = 0; level < tp_.minimize().size(); ++level)
        for (auto i : tp_.minimize()[level])
        {
            std::vector<order::View> mini;
            mini.emplace_back(i.second);
            if (n_->getConfig().optimizeOptimize) // optimize away equality and minimize variable
            {
                if (abs(i.second.a)==1)
                for (auto rlin = n_->linearConstraints_.begin(); rlin != n_->linearConstraints_.end(); ++rlin)
                {
                    if (mctx_.isTrue(rlin->v) && rlin->l.getRelation()==order::LinearConstraint::Relation::EQ && rlin->l.getConstViews().size()>1)
                    {
                        auto& views = rlin->l.getConstViews();
                        bool hit = false;
                        for (unsigned int j = 0; j <= views.size(); ++j)
                        {
                            if (views[j].v == i.second.v && views[j].a == -1)
                            {
                                hit = true;

                                order::LinearConstraint lc = rlin->l;
                                auto& newviews = lc.getViews();
                                newviews.erase(newviews.begin()+j);
                                lc.times(i.second.a);
                                mini.clear();
                                mini = std::move(newviews);
                                mini.front().c -= rlin->l.getRhs();
                                break;
                            }
                        }
                        if (hit)
                        {
                            //n.linearConstraints_.erase(rlin);
                            break;
                        }
                    }
                }

            }
            for (auto& j : mini)
                n_->addMinimize(j,level);
        }
}

}
