// Copyright (c) Facebook, Inc. and its affiliates.
// All rights reserved.
//
// This source code is licensed under the license found in the
// LICENSE file in the root directory of this source tree.

#include "Hanabi.h"
#include "InfoBot.h"
#include "BotFactory.h"

#include <cassert>
#include <memory>
#include <vector>
#include <algorithm>


static void _registerBots() {
  registerBotFactory("InfoBot", std::shared_ptr<Hanabi::BotFactory>(new ::BotFactory<InfoBot>()));
}

static int dummy =  (_registerBots(), 0);


#define MAXPLAYERS 10  // use statically sized arrays instead of vector in some places
#define MAXHANDSIZE 5  // use statically sized arrays instead of vector in some places

using Hanabi::Card;
using Hanabi::CardIndices;
using Hanabi::Color;
using Hanabi::Value;
using Hanabi::RED;
using Hanabi::BLUE;

template<class T, int Cap>
class fixed_capacity_vector {
    int size_ = 0;
    alignas(T) char buffer_[Cap][sizeof(T)];
public:
    fixed_capacity_vector() = default;
    explicit fixed_capacity_vector(int initial_size) noexcept {
        for (int i=0; i < initial_size; ++i) {
            this->emplace_back();
        }
    }
    fixed_capacity_vector(const fixed_capacity_vector& rhs) noexcept {
        for (const auto& elt : rhs) {
            this->emplace_back(elt);
        }
    }
    fixed_capacity_vector(fixed_capacity_vector&& rhs) noexcept {
        for (auto& elt : rhs) {
            this->emplace_back(std::move(elt));
        }
    }
    fixed_capacity_vector& operator=(const fixed_capacity_vector& rhs) noexcept {
      size_ = 0;
      for (auto &elt : rhs) {
        this->emplace_back(std::move(elt));
      }
      return *this;
    }

    ~fixed_capacity_vector() {
        for (int i=0; i < size_; ++i) {
            (*this)[i].~T();
        }
    }

    template<class... Args>
    void emplace_back(Args&&... args) {
        assert(size_ < Cap);
        new ((void*)buffer_[size_]) T(std::forward<Args>(args)...);
        size_ += 1;
    }

    void erase(T *it) {
        assert(this->begin() <= it && it < this->end());
        size_ -= 1;
        for (int i = (it - begin()); i < size_; ++i) {
            (*this)[i] = std::move((*this)[i+1]);
        }
        (*this)[size_].~T();
    }

    const T& operator[](int i) const { return *(const T*)buffer_[i]; }
    T& operator[](int i) { return *(T*)buffer_[i]; }
    const T& back() const { return *(const T*)buffer_[size_-1]; }
    T& back() { return *(T*)buffer_[size_-1]; }
    const T *begin() const { return (const T*)buffer_[0]; }
    const T *end() const { return (const T*)buffer_[size_]; }
    T *begin() { return (T*)buffer_[0]; }
    T *end() { return (T*)buffer_[size_]; }
    int size() const { return size_; }
    bool empty() const { return size() == 0; }
};

template<class T, int Cap>
class fixed_capacity_set {
    fixed_capacity_vector<T, Cap> elements_;
public:
    void insert(T t) {
        for (auto&& elt : elements_) {
            if (elt == t) return;
        }
        elements_.emplace_back(std::move(t));
    }
    const T *begin() const { return elements_.begin(); }
    const T *end() const { return elements_.end(); }
    T *begin() { return elements_.begin(); }
    T *end() { return elements_.end(); }
    int size() const { return elements_.size(); }
    bool empty() const { return elements_.empty(); }
};

struct Hinted {
    enum Kind { COLOR, VALUE };
    explicit Hinted(Kind kind, int k) : kind(kind), color(Color(-1)), value(Value(-1)) {
        if (kind == Hinted::COLOR) color = Color(k);
        if (kind == Hinted::VALUE) value = Value(k);
    }
    static Hinted WithColor(Color k) { return Hinted(COLOR, int(k)); }
    static Hinted WithValue(Value v) { return Hinted(VALUE, int(v)); }
    static Hinted Dummy() { return Hinted(COLOR, 0); }
    Kind kind;
    Color color;
    Value value;

    friend bool operator==(const Hinted& a, const Hinted& b) noexcept {
        return (a.kind == b.kind) && (a.color == b.color) && (a.value == b.value);
    }
};

struct Hint : public Hinted {
    int to;
    explicit Hint(Kind kind, int to, int k) : Hinted(kind, k), to(to) {}
};

class CardCounts {
    int8_t counts_[Hanabi::NUMCOLORS][5+1] {};
public:
    explicit CardCounts() = default;
    void increment(Card card) {
        counts_[card.color][card.value] += 1;
    }
    int remaining(Card card) const {
        return card.count() - counts_[card.color][card.value];
    }
};

class GameView {
public:
    CardCounts discard;
    int fireworks[Hanabi::NUMCOLORS];
    int deck_size;
    int total_cards;
    int num_players;
    int hand_size;
    int discard_size;
    int hints_remaining;
    int lives_remaining;
    int player; // whose turn is it?
private:
    int hints_total;
    int lives_total;
public:
    explicit GameView(const Hanabi::Server& server) {
        deck_size = server.cardsRemainingInDeck();
        total_cards = Hanabi::NUMCOLORS * 10;
        discard_size = server.discards().size();
        for (Card card : server.discards()) {
            discard.increment(card);
        }
        for (Color k = RED; k <= BLUE; ++k) {
            fireworks[k] = server.pileOf(Color(k)).size();
        }
        num_players = server.numPlayers();
        player = server.activePlayer();
        hand_size = server.handSize();
        hints_total = Hanabi::NUMHINTS;
        hints_remaining = server.hintStonesRemaining();
        lives_total = Hanabi::NUMMULLIGANS;
        lives_remaining = server.mulligansRemaining();
    }

    explicit GameView(int numPlayers, int handSize) {
        deck_size = Hanabi::NUMCOLORS * 10;
        total_cards = Hanabi::NUMCOLORS * 10;
        for (Color k = RED; k <= BLUE; ++k) {
            fireworks[k] = 0;
        }
        num_players = numPlayers;
        player = 0;
        hand_size = handSize;
        hints_total = Hanabi::NUMHINTS;
        hints_remaining = Hanabi::NUMHINTS;
        lives_total = Hanabi::NUMMULLIGANS;
        lives_remaining = Hanabi::NUMMULLIGANS;
    }

    // returns whether a card would place on a firework
    bool is_playable(Card card) const {
        return card.value == this->fireworks[card.color] + 1;
    }

    // best possible value we can get for firework of that color,
    // based on looking at discard + fireworks
    bool is_higher_than_highest_attainable(Card card) const {
        assert(card.value > this->fireworks[card.color]);
        assert(this->discard.remaining(card) != 0);  // called only on a card in someone's hand
        for (int v = this->fireworks[card.color] + 1; v < card.value; ++v) {
            Card needed_card(card.color, v);
            if (this->discard.remaining(needed_card) == 0) {
                // already discarded all of these
                return true;
            }
        }
        return false;
    }

    // is never going to play, based on discard + fireworks
    bool is_dead(Card card) const {
        return (
            card.value <= this->fireworks[card.color] ||
            this->is_higher_than_highest_attainable(card)
        );
    }

    // can be discarded without necessarily sacrificing score, based on discard + fireworks
    bool is_dispensable(Card card) const {
        return (this->discard.remaining(card) != 1) || this->is_dead(card);
    }
};

class OwnedGameView : public GameView {
    const Hanabi::Server *server;
public:
    explicit OwnedGameView(const GameView& view, const Hanabi::Server& server) : GameView(view), server(&server) {}

    const std::vector<Card>& get_hand(int p) const {
        static_assert(std::is_same<const std::vector<Card>&, decltype(server->handOfPlayer(p))>::value, "");
        return server->handOfPlayer(p);
    }

    bool has_card(int player, Card card) const {
        for (Card c : server->handOfPlayer(player)) {
            if (c == card) return true;
        }
        return false;
    }

    bool can_see(Card card) const {
        int me = server->whoAmI();
        for (int p=0; p < num_players; ++p) {
            if (p != me && this->has_card(p, card)) return true;
        }
        return false;
    }

    bool someone_else_can_play() const {
        int me = server->whoAmI();
        for (int p=0; p < server->numPlayers(); ++p) {
            if (p == me) continue;
            for (auto&& card : server->handOfPlayer(p)) {
                if (this->is_playable(card)) {
                    return true;
                }
            }
        }
        return false;
    }
};

class CardPossibilityTable {
    int8_t counts_[Hanabi::NUMCOLORS][5+1] {};
public:
    explicit CardPossibilityTable() {
        for (Color k = RED; k <= BLUE; ++k) {
            for (int v = 1; v <= 5; ++v) {
                counts_[k][v] = Hanabi::Card(k, v).count();
            }
        }
    }
    static CardPossibilityTable from(const CardCounts& counts) {
        CardPossibilityTable result;
        for (Color k = RED; k <= BLUE; ++k) {
            for (int v = 1; v <= 5; ++v) {
                result.counts_[k][v] = counts.remaining(Hanabi::Card(k, v));
            }
        }
        return result;
    }
    void mark_false(Card card) {
        counts_[card.color][card.value] = 0;
    }
    void mark_color(Color color, bool yes) {
        for (Color k = RED; k <= BLUE; ++k) {
            for (int v = 1; v <= 5; ++v) {
                if ((k == color) != yes) {
                    mark_false(Card(k, v));
                }
            }
        }
    }
    void mark_value(Value value, bool yes) {
        for (Color k = RED; k <= BLUE; ++k) {
            for (int v = 1; v <= 5; ++v) {
                if ((v == value) != yes) {
                    mark_false(Card(k, v));
                }
            }
        }
    }
    bool is_possible(Card card) const {
        return (counts_[card.color][card.value] != 0);
    }
    bool can_be_color(Color color) const {
        for (int v = 1; v <= 5; ++v) {
            if (counts_[color][v] != 0) return true;
        }
        return false;
    }
    bool can_be_value(Value value) const {
        for (Color k = RED; k <= BLUE; ++k) {
            if (counts_[k][value] != 0) return true;
        }
        return false;
    }
    void decrement_weight_if_possible(Card card) {
        auto& count = counts_[card.color][card.value];
        if (count) count -= 1;
    }
    template<class F>
    double weighted_score(const F& score_fn) const {
        double total_score = 0;
        int total_weight = 0;
        for (Color k = RED; k <= BLUE; ++k) {
            for (int v = 1; v <= 5; ++v) {
                if (counts_[k][v] != 0) {
                    int weight = counts_[k][v];
                    double score = score_fn(Card(k, v));
                    total_weight += weight;
                    total_score += weight * score;
                }
            }
        }
        return total_score / total_weight;
    }
    double average_value() const {
        return this->weighted_score([](Card card) { return int(card.value); });
    }
    int total_weight() const {
        int result = 0;
        this->for_each_possibility_by_count([&](Card, int count) { result += count; });
        return result;
    }
    template<class F>
    double probability_of_predicate(const F& predicate) const {
        return this->weighted_score([&](Card card) { return predicate(card) ? 1 : 0; });
    }
    double probability_is_dead(const GameView& view) const {
        int total_dead = 0;
        int total_live = 0;
        for (Color k = RED; k <= BLUE; ++k) {
            int next = view.fireworks[k] + 1;
            int v = 1;
            for (; v < next; ++v) {
                total_dead += counts_[k][v];
            }
            for (; v <= 5 && view.discard.remaining(Card(k, v)) != 0; ++v) {
                total_live += counts_[k][v];
            }
            for (; v <= 5; ++v) {
                total_dead += counts_[k][v];
            }
        }
        return total_dead / double(total_dead + total_live);
    }
    double probability_is_playable(const GameView& view) const {
        return this->probability_of_predicate([&](Card card) { return view.is_playable(card); });
    }
    double probability_is_dispensable(const GameView& view) const {
        return this->probability_of_predicate([&](Card card) { return view.is_dispensable(card); });
    }
    template<class F>
    void for_each_possibility(const F& fn) const {
        for (Color k = RED; k <= BLUE; ++k) {
            for (int v = 1; v <= 5; ++v) {
                if (counts_[k][v] != 0) {
                    fn(Card(k, v));
                }
            }
        }
    }
    template<class F>
    void for_each_possibility_by_count(const F& fn) const {
        for (Color k = RED; k <= BLUE; ++k) {
            for (int v = 1; v <= 5; ++v) {
                if (counts_[k][v] != 0) {
                    fn(Card(k, v), counts_[k][v]);
                }
            }
        }
    }
    bool is_determined() const {
        int count = 0;
        this->for_each_possibility([&](Card) { ++count; });
        return count == 1;
    }
    template<class F>
    void if_is_determined(const F& fn) const {
        int count = 0;
        this->for_each_possibility([&](Card) { ++count; });
        if (count == 1) {
            this->for_each_possibility(fn);
        }
    }
    bool color_determined() const {
        bool found = false;
        for (Color k = RED; k <= BLUE; ++k) {
            for (int v = 1; v <= 5; ++v) {
                if (counts_[k][v] != 0) {
                    if (found) return false;
                    found = true;
                    break;
                }
            }
        }
        assert(found);  // otherwise we didn't find any possibilities at all
        return false;
    }
    bool value_determined() const {
        bool found = false;
        for (int v = 1; v <= 5; ++v) {
            for (Color k = RED; k <= BLUE; ++k) {
                if (counts_[k][v] != 0) {
                    if (found) return false;
                    found = true;
                    break;
                }
            }
        }
        assert(found);  // otherwise we didn't find any possibilities at all
        return false;
    }
};

using HandInfo = fixed_capacity_vector<CardPossibilityTable, MAXHANDSIZE>;
using SinglePlayerHintSet = fixed_capacity_set<Hinted, 2*MAXHANDSIZE>;

struct ModulusInformation {
    int modulus;
    int value;

    explicit ModulusInformation(int m, int v) : modulus(m), value(v) {
        assert(value < modulus);
    }

    static ModulusInformation none() {
        return ModulusInformation(1, 0);
    }

    void combine(ModulusInformation other) {
        value = value * other.modulus + other.value;
        modulus *= other.modulus;
    }

    ModulusInformation split(int m, bool permissive) {
        assert(modulus >= m || permissive);
        assert(modulus % m == 0 || permissive);
        int original_modulus = modulus;
        int original_value = value;
        modulus /= m;
        int v = value / modulus;
        value -= (v * modulus);
        assert(original_modulus == m * modulus || permissive);
        assert(original_value == v * modulus + value || permissive);
        return ModulusInformation(m, std::min(v, m-1));
    }

    void cast_up(int m) {
        assert(modulus <= m);
        assert(value < m);
        modulus = m;
    }

    void cast_down(int m) {
        assert(modulus >= m);
        assert(value < m);
        modulus = m;
    }

    void add(ModulusInformation other) {
        assert(modulus == other.modulus);
        value = (value + other.value) % modulus;
    }

    void subtract(ModulusInformation other) {
        assert(modulus == other.modulus);
        value = (modulus + value - other.value) % modulus;
    }
};

class IsPlayable {
    int index;
public:
    IsPlayable() = default;
    explicit IsPlayable(int i) : index(i) {}

    int info_amount() const { return 2; }
    int answer(const std::vector<Card>& hand, const GameView& view) const {
        const Card& card = hand[this->index];
        if (view.is_playable(card)) {
            return 1;
        } else {
            return 0;
        }
    }
    void acknowledge_answer(int answer, HandInfo& hand_info, const GameView& view) const {
        auto& card_table = hand_info[this->index];
        card_table.for_each_possibility([&](Card card) {
            if (view.is_playable(card)) {
                if (answer == 0) { card_table.mark_false(card); }
            } else {
                if (answer == 1) { card_table.mark_false(card); }
            }
        });
    }
};

class CardToIntMap {
    int8_t map_[Hanabi::NUMCOLORS][5+1] {};
public:
    void emplace(Card key, int value) {
        map_[key.color][key.value] = value;
    }
    int at(Card key) const { return map_[key.color][key.value]; }
};

class CardPossibilityPartition {
    int index;
    int n_partitions;
    CardToIntMap partition;
public:
    CardPossibilityPartition() = default;
    explicit CardPossibilityPartition(
        int index, int max_n_partitions, const CardPossibilityTable& card_table, const GameView& view)
    {
        int cur_block = 0;
        CardToIntMap partition;
        int n_partitions = 0;

        bool has_dead = card_table.probability_is_dead(view) != 0.0;

        // TODO: group things of different colors and values?
        int effective_max = max_n_partitions;
        if (has_dead) {
            effective_max -= 1;
        }

        card_table.for_each_possibility([&](Card card) {
            if (!view.is_dead(card)) {
                partition.emplace(card, cur_block);
                cur_block = (cur_block + 1) % effective_max;
                if (n_partitions < effective_max) {
                    n_partitions += 1;
                }
            }
        });

        if (has_dead) {
            card_table.for_each_possibility([&](Card card) {
                if (view.is_dead(card)) {
                    partition.emplace(card, n_partitions);
                }
            });
            n_partitions += 1;
        }

        this->index = index;
        this->n_partitions = n_partitions;
        this->partition = std::move(partition);
    }

    int info_amount() const { return this->n_partitions; }
    int answer(const std::vector<Card>& hand, const GameView&) const {
        const auto& card = hand[this->index];
        return this->partition.at(card);
    }
    void acknowledge_answer(int answer, HandInfo& hand_info, const GameView&) const {
        auto& card_table = hand_info[this->index];
        card_table.for_each_possibility([&](Card card) {
            if (this->partition.at(card) != answer) {
                card_table.mark_false(card);
            }
        });
    }
};

class Question {
    enum Kind { Q_IS_PLAYABLE, Q_PARTITION };
    Kind kind;
    std::aligned_union_t<1, ::IsPlayable, ::CardPossibilityPartition> storage;

    template<class F>
    auto visit(const F& fn) const {
        switch (kind) {
            case Q_IS_PLAYABLE: return fn(*(::IsPlayable*)&this->storage);
            case Q_PARTITION: return fn(*(::CardPossibilityPartition*)&this->storage);
            default: assert(false); __builtin_unreachable();
        }
    }
public:
    int info_amount() const {
        return visit([&](auto&& impl){ return impl.info_amount(); });
    }
    int answer(const std::vector<Card>& hand, const GameView& view) const {
        return visit([&](auto&& impl){ return impl.answer(hand, view); });
    }
    void acknowledge_answer(int answer, HandInfo& info, const GameView& view) const {
        return visit([&](auto&& impl){ return impl.acknowledge_answer(answer, info, view); });
    }

    ModulusInformation answer_info(const std::vector<Card>& hand, const GameView& view) const {
        int m = this->info_amount();
        return ModulusInformation(
            m,
            std::min(m - 1, this->answer(hand, view))  // pick m-1 if confused
        );
    }

    void acknowledge_answer_info(ModulusInformation answer, HandInfo& hand_info, const GameView& view) const {
        assert(this->info_amount() == answer.modulus);
        this->acknowledge_answer(answer.value, hand_info, view);
    }

    static Question IsPlayable(int i) {
        Question result;
        result.kind = Q_IS_PLAYABLE;
        new (&result.storage) ::IsPlayable(i);
        return result;
    }
    static Question CardPossibilityPartition(
        int index, int max_n_partitions,
        const CardPossibilityTable& card_table, const GameView& view
    ) {
        Question result;
        result.kind = Q_PARTITION;
        new (&result.storage) ::CardPossibilityPartition(index, max_n_partitions, card_table, view);
        return result;
    }
};

struct AugmentedCardPossibilities {
    CardPossibilityTable card_table;
    int i;
    double p_play;
    double p_dead;
    bool is_determined;

    explicit AugmentedCardPossibilities(CardPossibilityTable ct, int i, const GameView& view) :
        card_table(ct), i(i)
    {
        int n_play = 0;
        int n_dead = 0;
        int n_unique = 0;
        int n_total = 0;
        ct.for_each_possibility_by_count([&](Card card, int count) {
            n_unique += 1;
            n_total += count;
            if (view.is_playable(card)) {
                n_play += count;
            } else if (view.is_dead(card)) {
                n_dead += count;
            }
        });
        this->p_play = n_play / (double)n_total;
        this->p_dead = n_dead / (double)n_total;
        this->is_determined = (n_unique == 1);
    }
};

class HintStrategyImpl {
protected:
    static int get_hint_index_score(const CardPossibilityTable& card_table, const GameView& view) {
        if (card_table.probability_is_dead(view) == 1.0) {
            return 0;
        }
        if (card_table.is_determined()) {
            return 0;
        }
        // Do something more intelligent?
        unsigned score = 0;
        card_table.for_each_possibility([&](Card card) {
            score |= (1u << int(card.color));
            score |= (1u << (int(card.value) + 5));
        });
        return __builtin_popcount(score);
    }

    static int get_index_for_hint(const HandInfo& info, const GameView& view) {
        fixed_capacity_vector<int, MAXHANDSIZE> scores;
        for (const auto& card_table : info) {
            scores.emplace_back(get_hint_index_score(card_table, view));
        }
        return std::max_element(scores.begin(), scores.end()) - scores.begin();
    }
};

class HintStrategy3 : public HintStrategyImpl {
    int card_index;
public:
    explicit HintStrategy3(const HandInfo& info, const GameView& view) {
        card_index = get_index_for_hint(info, view);
    }
    int get_count() const { return 3; }
    template<class F>
    void encode_hint(const OwnedGameView& view, int to, int hint_type, const F& fn) const {
        const auto& hand = view.get_hand(to);
        const auto& hint_card = hand[card_index];
        if (hint_type == 0) {
            fn( Hinted::WithValue(hint_card.value) );
        } else if (hint_type == 1) {
            fn( Hinted::WithColor(hint_card.color) );
        } else {
            for (auto&& card : hand) {
                if (card.color != hint_card.color) fn( Hinted::WithColor(card.color) );
                if (card.value != hint_card.value) fn( Hinted::WithValue(card.value) );
            }
        }
    }
    int decode_hint(Hint hint, CardIndices card_indices) const {
        if (card_indices.contains(card_index)) {
            if (hint.kind == Hinted::VALUE) {
                return 0;
            } else {
                return 1;
            }
        } else {
            return 2;
        }
    }
};

class HintStrategy4 : public HintStrategyImpl {
    int card_index;
public:
    explicit HintStrategy4(const HandInfo& info, const GameView& view) {
        card_index = get_index_for_hint(info, view);
    }
    int get_count() const { return 4; }
    template<class F>
    void encode_hint(const OwnedGameView& view, int to, int hint_type, const F& fn) const {
        const auto& hand = view.get_hand(to);
        const auto& hint_card = hand[card_index];
        if (hint_type == 0) {
            fn( Hinted::WithValue(hint_card.value) );
        } else if (hint_type == 1) {
            fn( Hinted::WithColor(hint_card.color) );
        } else if (hint_type == 2) {
            for (auto&& card : hand) {
                if (card.value != hint_card.value) fn( Hinted::WithValue(card.value) );
            }
        } else {
            for (auto&& card : hand) {
                if (card.color != hint_card.color) fn( Hinted::WithColor(card.color) );
            }
        }
    }
    int decode_hint(Hint hint, CardIndices card_indices) const {
        if (card_indices.contains(card_index)) {
            if (hint.kind == Hinted::VALUE) {
                return 0;
            } else {
                return 1;
            }
        } else {
            if (hint.kind == Hinted::VALUE) {
                return 2;
            } else {
                return 3;
            }
        }
    }
};

class HintStrategySetPacking {
    int count_;
    int color_to_int_[Hanabi::NUMCOLORS];
    int value_to_int_[5+1];
public:
    explicit HintStrategySetPacking(const HandInfo& info) {
        // Make a matrix with info.size() rows and NUMCOLORS columns.
        // Set each cell (r,c) to 1 if the r'th card could be of color c.
        // Find the max-cardinality set of disjoint rows; this is the
        // NP-complete "set-cover" problem, so we just do brute force
        // search over the at-most-32 possible row-sets.
        // Each row in the max-cardinality set corresponds to a
        // card in the hand where touching that card with a color hint
        // cannot possibly be confused with any other hint generated
        // by this algorithm.
        //
        int nrows = info.size();
        unsigned rowsetmax = (1u << nrows);
        unsigned largest_disjoint_color_rowset = 1;
        int best_cardinality = 1;
        for (unsigned rowset = 3; rowset < rowsetmax; ++rowset) {
            int cardinality_of_rowset = __builtin_popcount(rowset);
            if (cardinality_of_rowset <= best_cardinality) {
                // Go on only if this rowset has a chance of being a new "best".
                continue;
            }
            bool rowset_is_disjoint = true;
            unsigned kmask = 0;
            for (int i=0; i < nrows; ++i) {
                if (rowset & (1u << i)) {
                    info[i].for_each_possibility([&](Card card) {
                        unsigned km = (1u << int(card.color));
                        if (kmask & km) {
                            rowset_is_disjoint = false;
                        }
                        kmask |= km;
                    });
                }
            }
            if (rowset_is_disjoint) {
                best_cardinality = cardinality_of_rowset;
                largest_disjoint_color_rowset = rowset;
            }
        }

        // Also perform the same operation, but for value hints.
        //
        unsigned largest_disjoint_value_rowset = 1;
        best_cardinality = 1;
        for (unsigned rowset = 3; rowset < rowsetmax; ++rowset) {
            int cardinality_of_rowset = __builtin_popcount(rowset);
            if (cardinality_of_rowset <= best_cardinality) {
                // Go on only if this rowset has a chance of being a new "best".
                continue;
            }
            bool rowset_is_disjoint = true;
            unsigned vmask = 0;
            for (int i=0; i < nrows; ++i) {
                if (rowset & (1u << i)) {
                    info[i].for_each_possibility([&](Card card) {
                        unsigned vm = (1u << int(card.value));
                        if (vmask & vm) {
                            rowset_is_disjoint = false;
                        }
                        vmask |= vm;
                    });
                }
            }
            if (rowset_is_disjoint) {
                best_cardinality = cardinality_of_rowset;
                largest_disjoint_value_rowset = rowset;
            }
        }

        // Now generate our mapping from hints to ints.
        std::fill(color_to_int_, std::end(color_to_int_), -1);
        std::fill(value_to_int_, std::end(value_to_int_), -1);
        count_ = 0;
        for (int i = 0; i < nrows; ++i) {
            if (largest_disjoint_color_rowset & (1u << i)) {
                info[i].for_each_possibility([&](Card card) {
                    color_to_int_[card.color] = count_;
                });
                count_ += 1;
            }
            if (largest_disjoint_value_rowset & (1u << i)) {
                info[i].for_each_possibility([&](Card card) {
                    value_to_int_[card.value] = count_;
                });
                count_ += 1;
            }
        }

        // Some colors and values might not be touched by any of the rows
        // in our largest disjoint rowset. Permit those hints to be given,
        // since their "overt" information might be useful to the hintee.
        int cur_block = 0;
        for (Color k = RED; k <= BLUE; ++k) {
            if (color_to_int_[k] == -1) {
                color_to_int_[k] = cur_block;
                cur_block = (cur_block + 1) % count_;
            }
        }
        for (int v = 1; v <= 5; ++v) {
            if (value_to_int_[v] == -1) {
                value_to_int_[v] = cur_block;
                cur_block = (cur_block + 1) % count_;
            }
        }
    }
    int get_count() const { return count_; }
    template<class F>
    void encode_hint(const OwnedGameView& view, int to, int hint_type, const F& fn) const {
        assert(0 <= hint_type && hint_type < get_count());
        for (auto&& card : view.get_hand(to)) {
            if (color_to_int_[card.color] == hint_type) fn( Hinted::WithColor(card.color) );
            if (value_to_int_[card.value] == hint_type) fn( Hinted::WithValue(card.value) );
        }
    }
    int decode_hint(Hint hint, CardIndices) const {
        if (hint.kind == Hinted::COLOR) {
            return color_to_int_[hint.color];
        } else {
            return value_to_int_[hint.value];
        }
    }
};

class InfoBotImpl;

class HintStrategy {
    enum Kind { HS_HINT3, HS_HINT4, HS_SETPACKING };
    Kind kind;
    std::aligned_union_t<1, ::HintStrategy3, ::HintStrategy4, ::HintStrategySetPacking> storage;

    template<class F>
    auto visit(const F& fn) const {
        switch (kind) {
            case HS_HINT3: return fn(*(::HintStrategy3*)&this->storage);
            case HS_HINT4: return fn(*(::HintStrategy4*)&this->storage);
            case HS_SETPACKING: return fn(*(::HintStrategySetPacking*)&this->storage);
            default: assert(false); __builtin_unreachable();
        }
    }
public:
    int get_count() const {
        return visit([&](auto&& impl){ return impl.get_count(); });
    }
    template<class F>
    void encode_hint(const OwnedGameView& view, int to, int hint_type, const F& fn) const {
        return visit([&](auto&& impl){ return impl.encode_hint(view, to, hint_type, fn); });
    }
    int decode_hint(Hint hint, CardIndices card_indices) const {
        return visit([&](auto&& impl) { return impl.decode_hint(hint, card_indices); });
    }

    static HintStrategy Make3(const HandInfo& info, const GameView& view) {
        HintStrategy result;
        result.kind = HS_HINT3;
        new (&result.storage) ::HintStrategy3(info, view);
        return result;
    }
    static HintStrategy Make4(const HandInfo& info, const GameView& view) {
        HintStrategy result;
        result.kind = HS_HINT4;
        new (&result.storage) ::HintStrategy4(info, view);
        return result;
    }
    static HintStrategy MakeSetPacking(const HandInfo& info) {
        HintStrategy result;
        result.kind = HS_SETPACKING;
        new (&result.storage) ::HintStrategySetPacking(info);
        return result;
    }
};


class InfoBotImpl : public InfoBot {
    int me;
    int numPlayers;
    std::vector<HandInfo> public_info;
    CardCounts public_counts; // what any newly drawn card could be
    GameView last_view; // the view on the previous turn

public:
    InfoBotImpl(int index, int numPlayers, int handSize) :
        me(index), numPlayers(numPlayers), last_view(numPlayers, handSize)
    {
        for (int i=0; i < numPlayers; ++i) {
            this->public_info.emplace_back(HandInfo(handSize));
        }
    }

    Bot *clone() const override {
      InfoBotImpl *b = new InfoBotImpl(me, numPlayers, last_view.hand_size);
      b->public_info = public_info;
      b->public_counts = public_counts;
      b->last_view = last_view;
      b->permissive_ = permissive_;
      return b;
    }

    fixed_capacity_vector<Question, 2*MAXHANDSIZE> get_questions(int total_info, const GameView& view, const HandInfo& hand_info) const {
        fixed_capacity_vector<Question, 2*MAXHANDSIZE> questions;
        int info_remaining = total_info;
        auto add_question = [&](Question question) {
            info_remaining /= question.info_amount();
            questions.emplace_back(std::move(question));
            return info_remaining <= 1;
        };

        fixed_capacity_vector<AugmentedCardPossibilities, MAXHANDSIZE> augmented_hand_info;
        bool any_known_playable = false;
        for (int i=0; i < (int)hand_info.size(); ++i) {
            augmented_hand_info.emplace_back(hand_info[i], i, view);
            if (augmented_hand_info.back().p_play == 1.0) {
                any_known_playable = true;
            }
        }

        if (!any_known_playable) {
            fixed_capacity_vector<AugmentedCardPossibilities, MAXHANDSIZE> ask_play;
            for (auto&& knol : augmented_hand_info) {
                if (knol.is_determined) continue;
                if (knol.p_dead == 1.0) continue;
                if (knol.p_play == 1.0 || knol.p_play < 0.2) continue;
                ask_play.emplace_back(knol);
            }
            // sort by probability of play, then by index
            std::sort(ask_play.begin(), ask_play.end(), [](auto&& a, auto&& b) {
                // *higher* probabilities are better
                if (a.p_play != b.p_play) return (a.p_play > b.p_play);
                return (a.i < b.i);
            });

            for (const auto& knol : ask_play) {
                auto q = Question::IsPlayable(knol.i);
                if (add_question(std::move(q))) {
                    return questions;
                }
            }
        }

        fixed_capacity_vector<AugmentedCardPossibilities, MAXHANDSIZE> ask_partition;
        for (auto&& knol : augmented_hand_info) {
            if (knol.is_determined) continue;
            // TODO: possibly still valuable to ask?
            if (knol.p_dead == 1.0) continue;
            ask_partition.emplace_back(knol);
        }

        // sort by probability of play, then by index
        std::sort(ask_partition.begin(), ask_partition.end(), [](auto&& a, auto&& b) {
            // *higher* probabilities are better
            return std::tie(b.p_play, a.i) < std::tie(a.p_play, b.i);
        });

        for (const auto& knol : ask_partition) {
            auto q = Question::CardPossibilityPartition(knol.i, info_remaining, knol.card_table, view);
            if (add_question(std::move(q))) {
                return questions;
            }
        }

        return questions;
    }

    ModulusInformation get_hint_info_for_player(
        int player, int total_info,
        const fixed_capacity_vector<Question, 2*MAXHANDSIZE>& questions,
        const OwnedGameView& view) const
    {
        assert(player != me);
        const auto& hand = view.get_hand(player);
        ModulusInformation answer = ModulusInformation::none();
        for (const auto& question : questions) {
            auto new_answer_info = question.answer_info(hand, view);
            answer.combine(new_answer_info);
        }
        answer.cast_up(total_info);
        return answer;
    }

    ModulusInformation get_hint_sum_info(int total_info, const OwnedGameView& view) const {
        ModulusInformation sum_info(total_info, 0);
        for (int player = 0; player < numPlayers; ++player) {
            if (player == me) continue;
            auto questions = this->get_questions(total_info, view, this->public_info[player]);
            auto answer = this->get_hint_info_for_player(player, total_info, questions, view);
            sum_info.add(answer);
        }
        return sum_info;
    }

    void infer_own_from_hint_sum(ModulusInformation hint) {
        auto& hand_info = this->public_info[me];
        auto questions = this->get_questions(hint.modulus, this->last_view, hand_info);
        int product = 1;
        for (auto&& b : questions) product *= b.info_amount();
        if (permissive_) {
          product = std::max(product, hint.value + 1); // product > hint.value
        }
        hint.cast_down(product);
        {
            auto view = this->last_view;
            for (auto&& question : questions) {
                auto answer_info = hint.split(question.info_amount(), permissive_);
                question.acknowledge_answer_info(answer_info, hand_info, view);
            }
        }
    }

    void update_from_hint_sum(ModulusInformation hint, const OwnedGameView& view) {
        int hinter = view.player;
        for (int player = 0; player < numPlayers; ++player) {
            if (player != hinter && player != this->me) {
                auto& hand_info = this->public_info[player];
                auto questions = this->get_questions(hint.modulus, view, hand_info);
                if (true) {
                    auto hint_info = this->get_hint_info_for_player(player, hint.modulus, questions, view);
                    hint.subtract(hint_info);
                }

                const auto& hand = view.get_hand(player);
                // auto questions = this->get_questions(hint.modulus, view, hand_info);
                for (auto&& question : questions) {
                    auto answer = question.answer(hand, view);
                    question.acknowledge_answer(answer, hand_info, view);
                }
            }
        }
        if (this->me == hinter) {
            assert(hint.value == 0 || permissive_);
        } else {
            this->infer_own_from_hint_sum(hint);
        }
    }

    // how badly do we need to play a particular card
    double get_average_play_score(const OwnedGameView& view, const CardPossibilityTable& card_table) {
        return card_table.weighted_score([&](const Card& card) {
            return this->get_play_score(view, card);
        });
    }

    double get_play_score(const OwnedGameView& view, const Card& card) {
        int num_with = 1;
        if (view.deck_size > 0) {
            for (int player = 0; player < numPlayers; ++player) {
                if (player != this->me) {
                    if (view.has_card(player, card)) {
                        num_with += 1;
                    }
                }
            }
        }
        return (10.0 - int(card.value)) / num_with;
    }

    fixed_capacity_vector<int, MAXHANDSIZE> find_useless_cards(const GameView& view, const HandInfo& hand) {
        fixed_capacity_set<int, MAXHANDSIZE> useless;
        CardToIntMap seen;

        for (int i=0; i < hand.size(); ++i) {
            const auto& card_table = hand[i];
            if (card_table.probability_is_dead(view) == 1.0) {
                useless.insert(i);
            } else {
                card_table.if_is_determined([&](Card card) {
                    if (seen.at(card) != 0) {
                        // found a duplicate card
                        useless.insert(i);
                        useless.insert(seen.at(card) - 1);
                    } else {
                        seen.emplace(card, i + 1);
                    }
                });
            }
        }
        fixed_capacity_vector<int, MAXHANDSIZE> useless_vec;
        for (int i : useless) {
            useless_vec.emplace_back(i);
        }
        std::sort(useless_vec.begin(), useless_vec.end());
        return useless_vec;
    }

    void update_public_info_for_hint(const Hint& hint, CardIndices card_indices) {
        HandInfo& info = this->public_info[hint.to];

        if (hint.kind == Hinted::COLOR) {
            for (int i = 0; i < info.size(); ++i) {
                info[i].mark_color(hint.color, card_indices.contains(i));
            }
        } else if (hint.kind == Hinted::VALUE) {
            for (int i = 0; i < info.size(); ++i) {
                info[i].mark_value(hint.value, card_indices.contains(i));
            }
        } else {
            assert(false);
        }
    }

    void update_public_info_for_discard_or_play(GameView& view, int player, int index, Card card) {
        HandInfo& info = this->public_info[player];
        assert(info[index].is_possible(card) || permissive_);
        info.erase(info.begin() + index);

        // push *before* incrementing public counts
        if (view.deck_size != 0) {
            info.emplace_back(CardPossibilityTable::from(this->public_counts));
        }

        for (auto& info : this->public_info) {
            for (auto& card_table : info) {
                card_table.decrement_weight_if_possible(card);
            }
        }

        this->public_counts.increment(card);
    }

    HandInfo get_private_info(const Hanabi::Server& server) const {
        HandInfo info = this->public_info[this->me];
        for (auto& card_table : info) {
            for (int p=0; p < server.numPlayers(); ++p) {
                if (p == me) continue;
                for (Card card : server.handOfPlayer(p)) {
                    card_table.decrement_weight_if_possible(card);
                }
            }
        }
        return info;
    }

    // Returns the number of ways to hint the player.
    HintStrategy get_hint_strategy(int player) const {
        const auto& info = this->public_info[player];

        HintStrategy setpacking = HintStrategy::MakeSetPacking(info);

        if (setpacking.get_count() > 4) {
            return setpacking;
        }

        bool may_be_all_one_color = false;
        for (Color k = RED; k <= BLUE; ++k) {
            if (std::all_of(info.begin(), info.end(), [=](auto&& card) { return card.can_be_color(k); })) {
                may_be_all_one_color = true;
                break;
            }
        }

        bool may_be_all_one_number = false;
        for (int v = 1; v <= 5; ++v) {
            if (std::all_of(info.begin(), info.end(), [=](auto&& card) { return card.can_be_value(Value(v)); })) {
                may_be_all_one_number = true;
                break;
            }
        }

        if (!may_be_all_one_color && !may_be_all_one_number) {
            return HintStrategy::Make4(this->public_info[player], this->last_view);
        } else {
            return HintStrategy::Make3(this->public_info[player], this->last_view);
        }
    }

    void get_hint(Hanabi::Server& server) const {
        OwnedGameView view(this->last_view, server);

        // Can give up to 3(n-1) hints
        // For any given player with at least 4 cards, and index i, there are at least 3 hints that can be given.
        // 0. a value hint on card i
        // 1. a color hint on card i
        // 2. any hint not involving card i
        // However, if it is public info that the player has at least two colors
        // and at least two numbers, then instead we do
        // 2. any color hint not involving i
        // 3. any color hint not involving i

        // TODO: make it so space of hints is larger when there is
        // knowledge about the cards?

        fixed_capacity_vector<HintStrategy, MAXPLAYERS> strategies;
        int total_info = 0;
        for (int i = 0; i < numPlayers - 1; ++i) {
            int player = (this->me + 1 + i) % numPlayers;
            strategies.emplace_back(this->get_hint_strategy(player));
            total_info += strategies.back().get_count();
        }
        auto hint_info = this->get_hint_sum_info(total_info, view);

        int hint_type = hint_info.value;
        int player_amt = 0;
        while (hint_type >= strategies[player_amt].get_count()) {
            hint_type -= strategies[player_amt].get_count();
            player_amt += 1;
        }

        int hint_player = (this->me + 1 + player_amt) % numPlayers;
        assert(hint_player != this->me);

        Hinted best_hint = Hinted::Dummy();
        double best_goodness = -1;

        if (true) {
            // get post-hint hand_info
            const auto& hand = server.handOfPlayer(hint_player);
            auto hand_info = this->public_info[hint_player];
            int total_info  = 3 * (view.num_players - 1);
            auto questions = this->get_questions(total_info, view, hand_info);
            for (const auto& question : questions) {
                auto answer = question.answer(hand, view);
                question.acknowledge_answer(answer, hand_info, view);
            }

            strategies[player_amt].encode_hint(view, hint_player, hint_type, [&](Hinted hinted) {
                // How good is it to give this hint to the player?
                auto hypothetical_hand_info = hand_info;
                double goodness = 1.0;
                for (int i=0; i < (int)hand_info.size(); ++i) {
                    auto& card_table = hypothetical_hand_info[i];
                    auto card = hand[i];
                    if (card_table.probability_is_dead(view) == 1.0) {
                        continue;
                    }
                    if (card_table.is_determined()) {
                        continue;
                    }
                    int old_weight = card_table.total_weight();
                    if (hinted.kind == Hinted::COLOR) {
                        card_table.mark_color(hinted.color, hinted.color == card.color);
                    } else if (hinted.kind == Hinted::VALUE) {
                        card_table.mark_value(hinted.value, hinted.value == card.value);
                    } else {
                        assert(false);
                    }
                    int new_weight = card_table.total_weight();
                    assert(new_weight <= old_weight);
                    double bonus = (
                        card_table.is_determined() ? 2 :
                        card_table.probability_is_dead(view) == 1.0 ? 2 :
                        1
                    );
                    goodness *= bonus * double(old_weight) / new_weight;
                }
                if (goodness > best_goodness) {
                    best_goodness = goodness;
                    best_hint = hinted;
                }
            });
        }

        if (best_hint.kind == Hinted::COLOR) {
            return server.pleaseGiveColorHint(hint_player, best_hint.color);
        } else {
            return server.pleaseGiveValueHint(hint_player, best_hint.value);
        }
    }

    void infer_from_hint(const Hint& hint, int hinter, CardIndices card_indices, const OwnedGameView& view) {
        fixed_capacity_vector<HintStrategy, MAXPLAYERS-1> strategies;
        int total_info = 0;
        for (int i = 0; i < numPlayers - 1; ++i) {
            int player = (hinter + 1 + i) % numPlayers;
            strategies.emplace_back(this->get_hint_strategy(player));
            total_info += strategies.back().get_count();
        }

        int player_amt = (numPlayers + hint.to - hinter - 1) % numPlayers;

        int amt_from_prev_players = 0;
        for (int i=0; i < player_amt; ++i) {
            amt_from_prev_players += strategies[i].get_count();
        }
        int hint_type = strategies[player_amt].decode_hint(hint, card_indices);
        int hint_value = amt_from_prev_players + hint_type;
        ModulusInformation mod_info(total_info, hint_value);
        this->update_from_hint_sum(mod_info, view);
    }

    void pleaseMakeMove(Hanabi::Server& server) override
    {
        // we already stored the view
        OwnedGameView view(this->last_view, server);

        auto private_info = this->get_private_info(server);

        // Maybe play the best playable card...
        double best_score = -1.0;
        int best_index = -1;
        for (int i=0; i < private_info.size(); ++i) {
            const auto& card_table = private_info[i];
            if (card_table.probability_is_playable(view) == 1.0) {
                double score = this->get_average_play_score(view, card_table);
                if (score > best_score) {
                    best_score = score;
                    best_index = i;
                }
            }
        }
        if (best_index >= 0) {
            return server.pleasePlay(best_index);
        }

        // Maybe make a risky play...
        const int discard_threshold =
            view.total_cards
            - (Hanabi::NUMCOLORS * 5)
            - (view.num_players * view.hand_size);

        if (view.lives_remaining > 1 && view.discard_size <= discard_threshold) {
            for (int i=0; i < private_info.size(); ++i) {
                const auto& card_table = private_info[i];
                // Consider cards that are definitely either playable or dead; don't risk
                // throwing away a card that we will want later in the game.
                if (card_table.probability_of_predicate([&](Card card) {
                    return view.is_playable(card) || view.is_dead(card);
                }) != 1.0) {
                    continue;
                }
                double p = card_table.probability_is_playable(view);
                if (p > 0.75 && p > best_score) {
                    best_score = p;
                    best_index = i;
                }
            }
            if (best_index >= 0) {
                return server.pleasePlay(best_index);
            }
        }

        if (!server.discardingIsAllowed()) {
            return this->get_hint(server);
        }

        auto public_useless_indices = this->find_useless_cards(view, this->public_info[this->me]);
        auto useless_indices = this->find_useless_cards(view, private_info);

        if (view.discard_size <= discard_threshold) {
            // if anything is totally useless, discard it
            if (public_useless_indices.size() > 1) {
                auto info = this->get_hint_sum_info(public_useless_indices.size(), view);
                return server.pleaseDiscard(public_useless_indices[info.value]);
            } else if (!useless_indices.empty()) {
                // TODO: have opponents infer that i knew a card was useless
                // TODO: after that, potentially prefer useless indices that arent public
                return server.pleaseDiscard(useless_indices[0]);
            }
        }

        // hinting is better than discarding dead cards
        // (probably because it stalls the deck-drawing).
        if (view.hints_remaining > 0) {
            if (view.someone_else_can_play()) {
                return this->get_hint(server);
            }
        }

        // if anything is totally useless, discard it
        if (public_useless_indices.size() > 1) {
            auto info = this->get_hint_sum_info(public_useless_indices.size(), view);
            return server.pleaseDiscard(public_useless_indices[info.value]);
        } else if (!useless_indices.empty()) {
            return server.pleaseDiscard(useless_indices[0]);
        }

        // NOTE: the only conditions under which we would discard a potentially useful card:
        // - we have no known useless cards
        // - there are no hints remaining OR nobody else can play

        // Play the best discardable card
        for (int i=0; i < private_info.size(); ++i) {
            const auto& card_table = private_info[i];
            double probability_is_seen = card_table.probability_of_predicate([&](Card card) {
                return view.can_see(card);
            });
            double my_compval =
                20.0 * probability_is_seen
                + 10.0 * card_table.probability_is_dispensable(view)
                + card_table.average_value();

            if (my_compval > best_score) {
                best_score = my_compval;
                best_index = i;
            }
        }
        if (best_index >= 0) {
            return server.pleaseDiscard(best_index);
        } else {
            return server.pleaseDiscard(0);
        }
    }

    void pleaseObserveColorHint(const Hanabi::Server& server, int from, int to, Color color, CardIndices card_indices) override
    {
        assert(this->me == server.whoAmI());
        OwnedGameView view(this->last_view, server);
        Hint hint(Hinted::COLOR, to, int(color));
        this->infer_from_hint(hint, from, card_indices, view);
        this->update_public_info_for_hint(hint, card_indices);
    }

    void pleaseObserveValueHint(const Hanabi::Server& server, int from, int to, Value value, CardIndices card_indices) override
    {
        assert(this->me == server.whoAmI());
        OwnedGameView view(this->last_view, server);
        Hint hint(Hinted::VALUE, to, int(value));
        this->infer_from_hint(hint, from, card_indices, view);
        this->update_public_info_for_hint(hint, card_indices);
    }

    void pleaseObserveBeforeDiscard(const Hanabi::Server& server, int from, int card_index) override
    {
        assert(this->me == server.whoAmI());
        OwnedGameView view(this->last_view, server);
        auto known_useless_indices = this->find_useless_cards(
            this->last_view, this->public_info[from]
        );
        if (known_useless_indices.size() > 1) {
            // unwrap is safe because *if* a discard happened, and there were known
            // dead cards, it must be a dead card
            int value = -1;
            for (int i=0; i < (int)known_useless_indices.size(); ++i) {
                if (known_useless_indices[i] == card_index) {
                    value = i;
                    break;
                }
            }
            if (permissive_) value = std::max(value, 0);
            assert(value != -1);
            this->update_from_hint_sum(ModulusInformation(known_useless_indices.size(), value), view);
        }
        this->update_public_info_for_discard_or_play(this->last_view, from, card_index, server.activeCard());
    }

    void pleaseObserveBeforePlay(const Hanabi::Server& server, int from, int card_index) override
    {
        assert(this->me == server.whoAmI());
        this->update_public_info_for_discard_or_play(this->last_view, from, card_index, server.activeCard());
    }

    void pleaseObserveBeforeMove(const Hanabi::Server& server) override
    {
        assert(this->me == server.whoAmI());
        this->last_view = GameView(server);
    }
};

std::unique_ptr<InfoBot> InfoBot::makeImpl(int index, int numPlayers, int handSize)
{
    return std::make_unique<InfoBotImpl>(index, numPlayers, handSize);
}
