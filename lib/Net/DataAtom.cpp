#include "net/DataAtom.h"

#include <cassert>

using namespace net;

DataAtom::DataAtom() {
}

DataAtom::DataAtom(DataAtom const& da) {
  assert(0);
}

DataAtom::~DataAtom() {
}

char DataAtom::nextClassId() {
  static char classId = 0;
  return ++classId;
}

bool DataAtom::sameClass(DataAtom const& as) const {
  return this->getClassId() == as.getClassId();
}

DataAtomHolder::DataAtomHolder(util::SharedPtr<DataAtom> atom) : atom(atom) {
}
DataAtomHolder::DataAtomHolder(DataAtomHolder const& from) : atom(from.atom) {
}
void DataAtomHolder::operator=(DataAtomHolder const& from) {
  atom = from.atom;
}
bool DataAtomHolder::operator==(DataAtomHolder const& cmp) const {
  if (atom == cmp.atom)
    return true;
  if (!atom || !cmp.atom)
    return false;
  if (atom->getClassId() != cmp.atom->getClassId())
    return false;
  return *atom == *cmp.atom;
}
bool DataAtomHolder::operator<(DataAtomHolder const& cmp) const {
  if (atom == cmp.atom)
    return false;
  if (!atom || !cmp.atom)
    return !atom;
  if (atom->getClassId() != cmp.atom->getClassId())
    return (atom->getClassId() < cmp.atom->getClassId());
  return *atom < *cmp.atom;
}
DataAtomHolder::operator util::SharedPtr<DataAtom>() const {
  return atom;
}

