// Copyright (c) Facebook, Inc. and its affiliates.
// All rights reserved.
//
// This source code is licensed under the license found in the
// LICENSE file in the root directory of this source tree.

#include <string>
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <set>
#include <algorithm>
#include <atomic>
#include <cstring>
#include "Hanabi.h"
#include <chrono>
#include <ctime>
#include <thread>

#include "InfoBot.h"
#include "SmartBot.h"

#include "BotUtils.h"

using namespace Hanabi;
using namespace HanabiParams;



BotVec cloneBotVec(const BotVec &vec, int who) {
  BotVec newvec;
  newvec.reserve(vec.size());
  for(int p = 0; p < vec.size(); p++) {
    if (p == who) {
      newvec.push_back(std::shared_ptr<Bot>()); // don't need myself
    } else {
      newvec.push_back(std::shared_ptr<Bot>(vec[p]->clone()));
    }
  }
  return newvec;
}

////////////////////////////////////////////////////////////////////////////////
//////////////////////    Helper Functions   ///////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

std::string now() {
  std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  char timestr[100];
  std::strftime(timestr, sizeof(timestr), "%Y-%m-%d %H:%M:%S", std::localtime(&now));
  return std::string(timestr) + "  ";
}

std::string colorname(Hanabi::Color color)
{
    switch (color) {
        case Hanabi::RED: return "red";
        case Hanabi::ORANGE: return "orange";
        case Hanabi::YELLOW: return "yellow";
        case Hanabi::GREEN: return "green";
        case Hanabi::BLUE: return "blue";
        case Hanabi::INVALID_COLOR: return "Invalid_color";
    }
    assert(false);
    return "ERROR"; // compiler warning
}

std::string Move::toString() const {
  switch (type) {
    case INVALID_MOVE: return "??? invalid";
    case DISCARD_CARD: return "Discard " + std::to_string(value);
    case PLAY_CARD: return "Play " + std::to_string(value);
    case HINT_COLOR: return "Hint " + colorname((Color) value) + " to player " + std::to_string(to);
    case HINT_VALUE: return "Hint " + std::to_string(value) + " to player " + std::to_string(to);
  }
  return "ERROR";
}

bool operator<(const Move& l, const Move& r)
{
  return std::tie(l.type, l.value, l.to)
       < std::tie(r.type, r.value, r.to);
}
bool operator==(const Move& l, const Move& r)
{
  return std::tie(l.type, l.value, l.to)
      == std::tie(r.type, r.value, r.to);
}

bool operator!=(const Move& l, const Move& r)
{
  return !(l == r);
}


std::vector<Move> enumerateLegalMoves(const Server &server) {
  std::set<Move> moves;
  for (int i = 0; i < server.sizeOfHandOfPlayer(server.whoAmI()); i++) {
    moves.insert(Move(PLAY_CARD, i));
    if (server.discardingIsAllowed()) {
      moves.insert(Move(DISCARD_CARD, i));
    }
  }
  for (int p = 0; p < server.numPlayers(); p++) {
    if (p == server.whoAmI()) continue;
    Hand partner_hand = server.handOfPlayer(p);
    for (Card card : partner_hand) {
      if (server.hintStonesRemaining() > 0) {
        moves.insert(Move(HINT_COLOR, card.color, p));
        moves.insert(Move(HINT_VALUE, card.value, p));
      }
    }
  }
  return std::vector<Move>(moves.begin(), moves.end());
}

std::string handAsString(const Hand &hand)
{
  std::ostringstream oss;
  for (int j=0; j < (int)hand.size(); ++j) {
    oss << (j ? ',' : ' ') << hand[j].toString();
  }
  if (oss.str() == "") return "";
  return oss.str().substr(1);
}


void addToDeck(const std::vector<Card> &cards, DeckComposition &deck) {
  for (auto &card : cards) {
    deck[card]++;
  }
}
void removeFromDeck(const std::vector<Card> &cards, DeckComposition &deck) {
  for (auto &card : cards) {
    deck[card]--;
    if (deck[card] < 0) {
      std::cerr << "Invalid removeFromDeck: " << handAsString(cards) << std::endl;
      assert(false);
    }
  }
}

DeckComposition getCurrentDeckComposition(const Server &server, int who) {
  DeckComposition deck;
  for (Color color = (Color) 0; color < NUMCOLORS; ++color) {
    for (int value = ONE; value <= VALUE_MAX; ++ value) {
      Card c(color, value);
      deck[c] = c.count();
    }
  }

  // calculate what's left in the deck
  removeFromDeck(server.discards(), deck);
  for(int player = 0; player < server.numPlayers(); player++){
    if (who == -1 || player == who) continue; // who == -1 means public
    removeFromDeck(server.handOfPlayer(player), deck);
  }
  std::vector<Card> pile_cards;
  for(Color color = RED; color < NUMCOLORS; color++) {
    auto p = server.pileOf(color);
    for(Value value = ONE; value <= VALUE_MAX; value++) {
      if (p.contains((int) value)) {
        pile_cards.push_back(Card(color, value));
      }
    }
  }
  removeFromDeck(pile_cards, deck);

  return deck;
}

void execute_(int from, Move move, Server &server) {
  assert(from == server.whoAmI());
  if (move.type == PLAY_CARD) {
    server.pleasePlay(move.value);
  } else if (move.type == DISCARD_CARD) {
    server.pleaseDiscard(move.value);
  } else if (move.type == HINT_COLOR) {
    server.pleaseGiveColorHint(move.to, Color(move.value));
  } else if (move.type == HINT_VALUE) {
    server.pleaseGiveValueHint(move.to, Value(move.value));
  } else {
    throw std::runtime_error("Invalid move: " + move.toString());
  }
}

int moveToIndex(Move move, const Server &server) {
  int NUM_COLORS = 5;
  int NUM_RANKS = 5;
  int me = server.whoAmI();
  int maxDiscard = server.handSize();
  int maxPlay = server.handSize();
  int numPlayers = server.numPlayers();
  int maxRevealColor = (numPlayers - 1) * NUM_COLORS;

  int targetOffset = (move.to + numPlayers - me) % numPlayers;

  if (move.type == DISCARD_CARD) {
    return move.value;
  } else if (move.type == PLAY_CARD) {
    return move.value + maxDiscard;
  } else if (move.type == HINT_COLOR) {
    return (targetOffset - 1) * NUM_COLORS + move.value + maxDiscard + maxPlay;
  } else if (move.type == HINT_VALUE) {
    return (targetOffset - 1) * NUM_RANKS + move.value - ONE + maxDiscard + maxPlay + maxRevealColor;
  }
  assert(false);
}

int cardToIndex(Card card) {
  return ((int) card.color) * 5 + ((int) card.value - ONE); // card value starts at one
}

Card indexToCard(int index) {
  return Card((Color) (index / 5), index % 5 + ONE);
}

BoxedHand::BoxedHand(const Hand &hand) {
  static std::map<Hand, std::unique_ptr<Hand>> box;
  auto iter = box.find(hand);
  if (iter == box.end()) {
    pHand = new Hand(hand);
    box.emplace(hand, pHand); // owned by box!
  } else {
    pHand = iter->second.get();
  }
}


////////////////////////////////////////////////////////////////////////////////
//////////////////////   FactorizedBeliefs   ///////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

FactorizedBeliefs::FactorizedBeliefs(const Server &server, int player) : p_(player) {
  DeckComposition public_deck = getCurrentDeckComposition(server, -1);
  for (Color c = RED; c < NUMCOLORS; c++) {
    for (int v = ONE; v <= VALUE_MAX; v++) {
      Card card(c, v);
      for (int i = 0; i < 5; i++) {
        counts[i].set(cardToIndex(card), i < server.handSize() ? public_deck[card] : 0);
      }
    }
  }

  // nothing is revealed yet
  for (int i = 0; i < 5; ++i) {
    for (int j = 0; j < 5; ++j) {
      colorRevealed.set(i*5 + j, 0);
      rankRevealed.set(i*5 + j, 0);
    }
  }
  handSize = server.sizeOfHandOfPlayer(p_);
}

void FactorizedBeliefs::updateFromHint(const Move &move, const Hanabi::CardIndices &card_indices, const Server &server) {
  handSize = server.sizeOfHandOfPlayer(p_);
  assert(move.type == HINT_COLOR || move.type == HINT_VALUE);
  for (int j = 0; j < 25; j++) {
    Card card = indexToCard(j);
    int card_value = move.type == HINT_COLOR ? (int) card.color : (int) card.value;
    bool matches = card_value == move.value;
    for (int i = 0; i < handSize; i++) {
      bool consistent = card_indices.contains(i) ? matches : !matches;
      if (!consistent) {
        counts[i].set(j, 0);
      }
    }
  }

  if (move.type == HINT_COLOR) {
    for (int i = 0; i < handSize; ++i) {
      if (card_indices.contains(i)) {
        colorRevealed.set(i*5 + (int) move.value, 1);
        assert(checkSum(colorRevealed, i, 1));
      }
    }
  } else {
    int rank = (int) move.value - 1;
    assert(rank >= 0 && rank < 5);
    for (int i = 0; i < handSize; ++i) {
      if (card_indices.contains(i)) {
        rankRevealed.set(i*5 + rank, 1);
        assert(checkSum(rankRevealed, i, 1));
      }
    }
  }
}

void FactorizedBeliefs::updateFromRevealedCard(Card played_card, const DeckComposition &deck, const Server &server) {
  // handSize = server.sizeOfHandOfPlayer(p_);

// update V0 factorized beliefs
  int card_id = cardToIndex(played_card);
  int remaining = deck.at(played_card); // this is what was remaining *before* the draw

  // for (int i = 0; i < server.sizeOfHandOfPlayer(p_); i++) {
  for (int i = 0; i < handSize; i++) {
    if (!(counts[i].get(card_id) == remaining + 1 || counts[i].get(card_id) == 0)) {
      std::cerr << "handSize " << handSize << " i " << i << " card_id " << card_id << " remaining+1 " << (remaining + 1) << " count " << (int) counts[i].get(card_id) << std::endl;
      assert(0);
    }
    counts[i].set(card_id, counts[i].get(card_id) == 0 ? 0 : remaining);
  }
  // normalize(server.sizeOfHandOfPlayer(p_));
  // we can't normalize here because we have to update from draw first,
  // otherwise if the player just played the last of a card, the beliefs may
  // be empty
}

void FactorizedBeliefs::updateFromDraw(const DeckComposition &deck, int card_index, const Server &server) {
  handSize = server.sizeOfHandOfPlayer(p_);

  // from my draw
  for (int i = card_index; i < std::min(handSize, server.handSize() - 1); i++) {
    for (int j = 0; j < 25; j++) {
      counts[i].set(j, counts[i + 1].get(j));
    }
    for (int j = 0; j < 5; ++j) {
      colorRevealed.set(i*5 + j, colorRevealed.get((i + 1)*5 + j));
      rankRevealed.set(i*5 + j, rankRevealed.get((i + 1)*5 + j));
    }
  }
  if (handSize == server.handSize()) {
    // draw the new card
    for (int j = 0; j < 25; j++) {
      counts[handSize - 1].set(j, 0);
    }
    for (const auto &kv : deck) {
      counts[handSize - 1].set(cardToIndex(kv.first), kv.second);
    }

    // we know nothing about this new card
    for (int j = 0; j < 5; ++j) {
      colorRevealed.set((handSize - 1)*5 + j, 0);
      rankRevealed.set((handSize - 1)*5 + j, 0);
    }
  } else {
    // no more card to draw, nil the last card
    assert(handSize < server.handSize());
    for (int j = 0; j < 25; j++) {
      counts[handSize].set(j, 0);
    }

    // nil the last card
    for (int j = 0; j < 5; ++j) {
      colorRevealed.set(handSize*5 + j, 0);
      rankRevealed.set(handSize*5 + j, 0);
    }
  }
  // normalize(hand_size);
}

std::array<std::array<float, 25>, 5> FactorizedBeliefs::get() const {
  std::array<std::array<float, 25>, 5> res;
  assert(handSize <= 5);
  for (int i = 0; i < handSize; i++) {
    double total = 0;
    for (int j = 0; j < 25; j++) {
      total += counts[i].get(j);
    }
    if (total <= 0) {
      printf("Total is 0 at %d\n", i);
      assert(0);
    }
    for (int j = 0; j < 25; j++) {
      res[i][j] = counts[i].get(j) / total;
    }
  }
  for (int i = handSize; i < counts.size(); i++) {
    for (int j = 0; j < 25; j++) {
      res[i][j] = 0;
    }
  }
  return res;
}

void FactorizedBeliefs::log() {
  auto beliefs = get();
  std::cerr << "V0 beliefs (player " << p_ << "): \n";
  for (int i = 0; i < handSize; i++) {
    for (int j = 0; j < 25; j++) {
      if (j % 5 == 0) std::cerr << std::endl << colorname(Color(j / 5))[0] << ": ";
      std::cerr << beliefs[i][j] << " ";
    }
    std::cerr << std::endl << std::endl;
  }
 }


////////////////////////////////////////////////////////////////////////////////
//////////////////////    SimulServer    ///////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

 SimulServer::SimulServer(int numPlayers)
     : Server()
     , mock_(false)
     , last_move_()
 {
   numPlayers_ = numPlayers;
 }

 SimulServer::SimulServer(const Server &server)
      : Server()
      , mock_(false)
      , last_move_()
  {
    numPlayers_ = server.numPlayers();
    sync(server);
  }

 void SimulServer::setPlayers(BotVec players) {
   players_.clear();
   for(auto &p : players) {
     players_.push_back(p.get());
   }
 }

 void SimulServer::incrementActivePlayer() {
   activePlayer_ = (activePlayer_ + 1) % numPlayers_;
   assert(0 <= finalCountdown_ && finalCountdown_ <= numPlayers_);
   if (deck_.empty()) finalCountdown_ += 1;
 }

 void SimulServer::setHand(int index, std::vector<Card> hand) {
   hands_[index] = hand;
 }

 void SimulServer::setDeck(const std::vector<Card> &deck) {
   deck_ = deck;
 }

 void SimulServer::setObservingPlayer(int observingPlayer) {
   observingPlayer_ = observingPlayer;
 }

 void SimulServer::sync(const Server &s) {
   observingPlayer_ = s.whoAmI();
   activePlayer_ = s.activePlayer();
   movesFromActivePlayer_ = 0;
   try {
     activeCard_ = s.activeCard();
     activeCardIsObservable_ = true;
   } catch (ServerError e) {
     activeCard_ = Card(INVALID_COLOR, 1);
     activeCardIsObservable_ = false;
   }
   finalCountdown_ = s.finalCountdown();

   for (int i = 0; i < NUMCOLORS; i++) {
     piles_[i] = s.pileOf((Color) i);
   }
   discards_ = s.discards();
   hintStonesRemaining_ = s.hintStonesRemaining();
   mulligansRemaining_ = s.mulligansRemaining();

   hands_.clear();
   for (int player = 0; player < s.numPlayers(); player++) {
     if (player == s.whoAmI()) {
       // fill my hand with bogus cards
       hands_.push_back(Hand(s.sizeOfHandOfPlayer(player), Card(INVALID_COLOR, 1)));
     } else {
       hands_.push_back(s.handOfPlayer(player));
     }
   }

   // make sure the deck is the right size, but fill with junk
   deck_.clear();
   for (int i = 0; i < s.cardsRemainingInDeck(); i++) {
     deck_.push_back(Card(INVALID_COLOR, 1));
   }
 }

 Move SimulServer::simulatePlayerMove(int index, Bot *bot) {
   mock_ = true;
   last_move_ = Move();
   activePlayer_ = observingPlayer_ = index;
   bot->pleaseMakeMove(*this);
   assert(last_move_.type != INVALID_MOVE);
   Move ret = last_move_;
   last_move_ = Move();
   mock_ = false;
   return ret;
 }

 void SimulServer::pleaseDiscard(int index) {
   if (!mock_) {
     Server::pleaseDiscard(index);
   }
   last_move_ = Move(DISCARD_CARD, index);
 }

 void SimulServer::pleasePlay(int index) {
   if (!mock_) {
     Server::pleasePlay(index);
   }
   last_move_ = Move(PLAY_CARD, index);
 }

 void SimulServer::pleaseGiveColorHint(int player, Color color) {
   if (!mock_) {
     Server::pleaseGiveColorHint(player, color);
   }
   last_move_ = Move(HINT_COLOR, (int) color, player);
 }

 void SimulServer::pleaseGiveValueHint(int player, Value value) {
   if (!mock_) {
     Server::pleaseGiveValueHint(player, value);
   }
   last_move_ = Move(HINT_VALUE, (int) value, player);
 }

 void SimulServer::applyToAll(ObservationFunc f, HandDist &hand_distribution, int me, bool update_me) {
   observingPlayer_ = me;
   if (update_me) {
     f(players_[me], *this);
   }
   std::cerr << now() << "applyToAll begin : " << hand_distribution.size() << " hands." << std::endl;
   auto hand_dist_keys = copyKeys(hand_distribution);
   std::vector<boost::fibers::future<void>> futures;
   for (int t = 0; t < NUM_THREADS; t++) {
     futures.push_back(getThreadPool().enqueue([&, t]() {
       auto simulserver = std::make_shared<SimulServer>(*this);
       auto fp = std::make_shared<ObservationFunc>(f);
       for (int i = t; i < hand_dist_keys.size(); i += NUM_THREADS) {
         auto &hand = hand_dist_keys[i];
         hand_distribution.at(hand).delayed_observations.emplace_back(
           simulserver, fp, me, hand
         );
         // simulserver.hands_[me] = hand;
         // for (int p = 0; p < numPlayers(); p++) {
         //   if (p == me) continue;
         //   simulserver.observingPlayer_ = p;
         //   f(hand_distribution.at(hand).partners[p].get(), simulserver);
         // }
       }
     }));
   }
   for (auto &f: futures) {
     f.get();
   }
   std::cerr << now() << "applyToAll end" << std::endl;
 }

void HandDistVal::applyObservations() {
  for (int p = 0; p < partners.size(); p++) {
    if (partners[p]) {
      partners[p] = getPartner(p);
    }
  }
  delayed_observations.clear();
}

std::shared_ptr<Bot> HandDistVal::getPartner(int who) const {
  auto bot = std::shared_ptr<Bot>(partners[who]->clone());
  for (auto &obs: delayed_observations) {
    SimulServer simulserver(*obs.server);
    simulserver.setHand(obs.who, obs.hand);
    assert (who != obs.who);
    simulserver.setObservingPlayer(who);
    (*obs.func)(bot.get(), simulserver);
  }
  return bot;
}

 // handDistCDF

 HandDistCDF populateHandDistPDF(const HandDist &handDist) {
   HandDistCDF cdf;
   cdf.hands.reserve(handDist.size());
   cdf.probs.reserve(handDist.size());
   for (auto &kv : handDist) {
     cdf.hands.push_back(kv.first);
     cdf.probs.push_back(kv.second.prob);
   }
   return cdf;
 }

 void pdfToCdf(const HandDistCDF &pdf, HandDistCDF &cdf) {
   // pdf and cdf can be the same object
   double total_prob = 0;
   size_t N = pdf.probs.size();
   assert(cdf.probs.size() == N);
   for (int i = 0; i < N; i++) {
     total_prob += pdf.probs[i];
   }
   assert(total_prob > 0);
   double accum = 0;
   for (int i = 0; i < N; i++) {
     double pdfi = pdf.probs[i]; // in case pdf == cdf
     cdf.probs[i] = accum / total_prob;
     accum += pdfi;
   }
 }

 HandDistCDF populateHandDistCDF(const HandDist &handDist) {
   HandDistCDF cdf = populateHandDistPDF(handDist);
   pdfToCdf(cdf, cdf);
   return cdf;
 }
