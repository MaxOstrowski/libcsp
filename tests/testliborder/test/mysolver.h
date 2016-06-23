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

#pragma once
#include "order/solver.h"
#include <stdexcept>
#include <algorithm>


class MySolver : public order::CreatingSolver
{
public:
    using Literal = order::Literal;
    using LitVec = order::LitVec;
    MySolver() : lits_(2) {}
    bool isTrue(Literal l) const { return l==trueLit(); }
    bool isFalse(Literal l) const { return l==falseLit(); }
    bool isUnknown(Literal l) const { return (l!=trueLit() && l != falseLit()); }

    /// set the literal to true/false, precondition - must be unknown
    // precondition unknown ? or what happens on failure ?
    bool set(Literal){ return true; }

    std::size_t numVars() const { return lits_-1; }

    void makeRestFalse()
    {

    }

    virtual void createNewLiterals(std::size_t )
    {

    }
    
    void freeze(Literal ) {}

    void unrestrictedDomainCallback(order::View) const
    {
        throw std::runtime_error("Variable was unrestricted");
    }
    
    void intermediateVariableOutOfRange() const
    {
        throw std::runtime_error("Intermediate Variable out of bounds (32bit integer)");
    }




    Literal getNewLiteral(bool ) { /*std::cout << "create lit" << lits_ << std::endl;*/ return Literal(lits_++,false); }
    Literal trueLit() const { return Literal(1, false); }
    Literal falseLit() const { return ~trueLit(); }

    bool createClause(const LitVec& lvv)
    {
        LitVec lv(lvv);
        lv.erase(std::remove_if(lv.begin(), lv.end(),[&](const Literal& l){ return isFalse(l); }),lv.end());
        for (auto i : lv)
            if (isTrue(i))
                return true;
//                for (auto i : lv)
//                {
//                    std::cout << i << ", ";
//                }
//                std::cout << "0,";
//                std::cout << std::endl;
        clauses_.insert(clauses_.end(), lv.begin(), lv.end());
        clauses_.emplace_back(Literal::fromRep(0)); // place holder
        return true;
    }


    bool createCardinality(Literal , int , LitVec&&)
    {
        assert(false); /// not implemented for sat solver clasp yet
        return false;
    }

    bool setEqual(const Literal &a, const Literal &b)
    {
        if (!createClause({a,~b})) return false;
        if (!createClause({~a,b})) return false;
        return true;
    }

    void printDimacs(std::ostream& out)
    {
        out << "p cnf " << lits_-1 << " " << std::count(clauses_.begin(), clauses_.end(), Literal::fromRep(0)) << "\n";
        for (auto i : clauses_)
        {
            out << (i.sign() ? -int(i.var()) : int(i.var()));
            if (i.asUint()==0)
                out << "\n";
            else
                out << " ";
        }
        out << "1 0\n";
    }

    void addMinimize(order::Literal , int32 , unsigned int )
    {
        assert(false); /// not implemented for sat solver clasp yet
    }

    const LitVec& clauses() const { /*std::cout << std::endl;*/ return clauses_; }

private:
    std::size_t lits_;
    LitVec clauses_;


};
