// Copyright (c) Facebook, Inc. and its affiliates.
// All rights reserved.
//
// This source code is licensed under the license found in the
// LICENSE file in the root directory of this source tree.

#pragma once

#include "Hanabi.h"
#include "BotFactory.h"

#include <memory>
#include <map>
#include <functional>
#include <fstream>
#include <array>

typedef enum {PLAY_CARD, DISCARD_CARD, HINT_COLOR, HINT_VALUE, INVALID_MOVE } MoveType;

const int NUMMOVETYPES = 5;


std::string now();

struct Move {
  MoveType type;
  int value;
  int to;
  Move() : type(INVALID_MOVE), value(-1), to(-1) {}
  Move(MoveType type, int value, int to=-1) : type(type), value(value), to(to) {
    assert((type == HINT_COLOR || type == HINT_VALUE) == (to != -1));
  }

  std::string toString() const;
};

bool operator<(const Move& l, const Move& r);
bool operator==(const Move& l, const Move& r);
bool operator!=(const Move& l, const Move& r);

std::vector<Move> enumerateLegalMoves(const Hanabi::Server &server);

typedef std::vector<Hanabi::Card> Hand;

#ifndef CARD_ID
// shortcut
template <>
inline bool std::operator< (const Hand &l, const Hand &r) {
  if (l.size() != r.size()) return l.size() < r.size();
  return memcmp(&l[0], &r[0], l.size() * sizeof(Hanabi::Card)) < 0;
}
#endif

std::string handAsString(const Hand &hand);

std::string colorname(Hanabi::Color color);

/* A map from card to the number of cards of that value remaining in the deck. */
typedef std::vector<std::shared_ptr<Hanabi::Bot>> BotVec;

BotVec cloneBotVec(const BotVec &vec, int who);

typedef std::function<void(Hanabi::Bot*, const Hanabi::Server &server)> ObservationFunc;


// deck composition
typedef std::map<Hanabi::Card, int> DeckComposition;
void addToDeck(const std::vector<Hanabi::Card> &cards, DeckComposition &deck);
void removeFromDeck(const std::vector<Hanabi::Card> &cards, DeckComposition &deck);
DeckComposition getCurrentDeckComposition(const Hanabi::Server &server, int who);

// search stats
struct UCBStats {
  static constexpr int MIN_SAMPLES = 100;
  static constexpr int BASELINE_MIN_SAMPLES = 35;
  static constexpr float STDS = 2;
  static constexpr float EPS = 0.01;

  UCBStats() {}
  bool pruned = false;
  double mean = 0;
  double M2 = 0;
  int N = 0;
  double bias = 0;
  void add(double x) {
    N += 1;
    double delta = x - mean;
    mean += delta / N;
    M2 += delta * (x - mean);
  }
  double std() const {
    return (N < MIN_SAMPLES) ? 1000000 : sqrt(M2 / (N - 1));
  }
  double stderr() const {
    return std() / sqrt(N);
  }
  double search_baseline_stderr() const {
    return (N < BASELINE_MIN_SAMPLES) ? 1000000 : sqrt(M2 / (N - 1)) / sqrt(N);
  }
  double lcb() const {
    return mean - STDS * stderr() + bias;
  }
  double ucb() const {
    return mean + STDS * stderr() + bias;
  }
};
typedef std::map<Move, UCBStats> SearchStats;

void execute_(int from, Move move, Hanabi::Server &server);

struct TwoBitArray {
  TwoBitArray() {}

  uint64_t x_;

  const uint8_t get(int index) const {
    assert(index >= 0 && index < 32);
    return ((x_ >> (index * 2)) & 0x3);
  }
  void set(int index, uint64_t value) {
    assert(index >= 0 && index < 32);
    assert(value >= 0 && value < 4);
    x_ = ((x_ & ~((uint64_t) 0x3 << (index * 2))) | (value << (index * 2)));
  }
};

struct FactorizedBeliefs {
  FactorizedBeliefs(const Hanabi::Server &server, int player);
  std::array<std::array<float, 25>, 5> get() const;
  void log();
  void updateFromHint(const Move &move, const Hanabi::CardIndices &card_indices, const Hanabi::Server &server);
  void updateFromRevealedCard(Hanabi::Card played_card, const DeckComposition &deck, const Hanabi::Server &server);
  void updateFromDraw(const DeckComposition &deck, int card_index, const Hanabi::Server &server);

  std::array<TwoBitArray, 5> counts;

  TwoBitArray colorRevealed;
  TwoBitArray rankRevealed;
  int handSize;

private:
  bool checkSum(const TwoBitArray& array, int card, int expectVal) {
    int sum = 0;
    int size = 5;
    for (int i = 0; i < size; ++i) {
      sum += array.get(5 * card + i);
    }
    if (sum != expectVal) {
      std::cout << "checkSum: " << sum << " vs "  << expectVal << std::endl;
    }
    return sum == expectVal;
  }

  int p_;
};

int moveToIndex(Move move, const Hanabi::Server &server);
int cardToIndex(Hanabi::Card card);
Hanabi::Card indexToCard(int index);

// a memory optimization to store hands more efficiently
class BoxedHand {
public:
  BoxedHand(const Hand &hand);
  BoxedHand(const BoxedHand &hand) { pHand = hand.pHand; }

  operator const Hand&() const { return *pHand; }

  const Hanabi::Card &operator[] (int index) const { return (*pHand)[index]; }
  const Hand& get() { return *pHand; }
  int size() {return pHand->size(); }
  bool operator== (const BoxedHand &r) const { return this->pHand == r.pHand; }
  bool operator!= (const BoxedHand &r) const { return this->pHand != r.pHand; }
  bool operator< (const BoxedHand &r) const { return this->pHand < r.pHand; }

private:
  Hand *pHand;
};

class SimulServer;
struct ObservationThunk {
  std::shared_ptr<SimulServer> server;
  std::shared_ptr<ObservationFunc> func;
  int who;
  BoxedHand hand;
  ObservationThunk(
    std::shared_ptr<SimulServer> server,
    std::shared_ptr<ObservationFunc> func,
    int who,
    BoxedHand hand)
    : server(server)
    , func(func)
    , who(who)
    , hand(hand) {}
};

typedef std::vector<ObservationThunk> ObservationList;

struct HandDistVal {
  float prob;
  ObservationList delayed_observations;

  HandDistVal(): prob(0), partners() {}
  HandDistVal(float prob, BotVec partners): prob(prob), partners(partners) {}

  void applyObservations();
  std::shared_ptr<Hanabi::Bot> getPartner(int who) const;

private:
  BotVec partners; // these are lazily updated so should only be accessed through getPartner()!
};

typedef std::map<BoxedHand, HandDistVal> HandDist;


class SimulServer : public Hanabi::Server {
public:
  SimulServer(int numPlayers);
  SimulServer(const Hanabi::Server &server);
  ~SimulServer() override { }
  void setPlayers(BotVec player);
  void setObservingPlayer(int observingPlayer);
  void incrementActivePlayer();

  /* allow the bot to munge the server */

  /* Copy over state from s so that the game on this server is consistent
   * with all information to the observing player. The hidden information
   * (my hand, the deck) are filled with junk cards. */
  virtual void sync(const Hanabi::Server &s);
  void setHand(int index, Hand my_hand);
  void setDeck(const std::vector<Hanabi::Card> &deck);

  /* Simulate the bot making a move, and return what the move was. */
  Move simulatePlayerMove(int index, Hanabi::Bot *bot);
  /* Apply an observation function (e.g. pleaseObserveBeforePlay) to all bots.
   * This observation is applied to the server's bot for me_, and to all
   * possible partner bots corresponding to every possible hand I may have in
   * my hand distribution. */
   // n.b. the map is const, the values are not const!
  void applyToAll(ObservationFunc f, HandDist &hand_distribution, int me, bool update_me=true);


  /* override mutator API for mocking */

  void pleaseDiscard(int index) override;
  void pleasePlay(int index) override;
  void pleaseGiveColorHint(int player, Hanabi::Color color) override;
  void pleaseGiveValueHint(int player, Hanabi::Value value) override;
  bool mock_;  // if true, mock out all the pleaseXXX methods, and just record the move
  Move last_move_;
};


template<typename K, typename V>
std::vector<K> copyKeys(const std::map<K, V>& map) {
  std::vector<K> res;
  for (auto &kv : map) {
    res.push_back(kv.first);
  }
  return res;
}

struct HandDistCDF {
  std::vector<double> probs;
  std::vector<BoxedHand> hands;
};

/* Converts the hand belief distribution into a CDF form that can be sampled
 * from efficiently */
HandDistCDF populateHandDistPDF(const HandDist &handDist);
void pdfToCdf(const HandDistCDF &pdf, HandDistCDF &cdf);
HandDistCDF populateHandDistCDF(const HandDist &handDist);
