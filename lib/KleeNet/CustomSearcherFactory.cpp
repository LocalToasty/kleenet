#include "kleenet/CustomSearcherFactory.h"

#include <map>
#include <list>

namespace kleenet {
  struct FactoryContainer {
    // Why list: a) we need random remove; b) We really really really do not care about speed!
    typedef std::list<CustomSearcherFactory*> Factories;
    typedef std::map<CustomSearcherFactory::Precedence, Factories> Member;
    Member member;
  };
}

using namespace kleenet;

FactoryContainer& CustomSearcherFactory::fetchContainer() {
  static FactoryContainer container;
  return container;
}

void CustomSearcherFactory::registerFactory(Precedence precedence, CustomSearcherFactory* factory) {
  fetchContainer().member[precedence].push_back(factory);
}
void CustomSearcherFactory::unregisterFactory(CustomSearcherFactory* factory) {
  FactoryContainer::Member& m(fetchContainer().member);
  for (FactoryContainer::Member::iterator it = m.begin(), en = m.end(); it != en; ++it) {
    it->second.remove(factory);
  }
}

Searcher* CustomSearcherFactory::attemptConstruction(Precedence precedence, net::PacketCacheBase* pcb) {
  FactoryContainer::Factories& f(fetchContainer().member[precedence]);
  for (FactoryContainer::Factories::const_iterator i = f.begin(), e = f.end(); i != e; ++i) {
    Searcher* const result((*i)->createSearcher(pcb));
    if (result)
      return result;
  }
  return NULL;
}
