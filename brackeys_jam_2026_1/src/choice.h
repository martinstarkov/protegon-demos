#pragma once

#include "protegon/protegon.h"

std::mt19937 rng(std::random_device{}());

struct Entry {
	int human_trait_count{ 0 };
	int planet_trait_count{ 0 };
	std::vector<std::pair<int, int>> patterns; // {good_count, bad_count}
};

inline Entry ParseEntry(const json& j) {
	Entry e;
	e.human_trait_count	 = j.at("human_trait_count").get<int>();
	e.planet_trait_count = j.at("planet_trait_count").get<int>();

	const auto& jp = j.at("patterns");
	e.patterns.reserve(jp.size());

	for (const auto& pat : jp) {
		// pattern is [good_count, bad_count]
		e.patterns.emplace_back(pat.at(0).get<int>(), pat.at(1).get<int>());
	}
	return e;
}

// For variable size arrays
inline std::vector<Entry> ParseEntries(const json& root_array) {
	if (!root_array.is_array()) {
		throw std::runtime_error("Expected top-level JSON array");
	}

	std::vector<Entry> out;
	out.reserve(root_array.size());
	for (const auto& item : root_array) {
		out.push_back(ParseEntry(item));
	}
	return out;
}

enum class TraitQuality {
	BAD,
	GOOD,
	RANDOM
};

std::ostream& operator<<(std::ostream& os, TraitQuality quality) {
	switch (quality) {
		case TraitQuality::BAD:	   os << "bad"; break;
		case TraitQuality::GOOD:   os << "good"; break;
		case TraitQuality::RANDOM: os << "random"; break;
	}
	return os;
}

struct Trait {
	std::string description;
	TraitQuality quality;
	std::string category;
	bool used = false;
};

// ---- DATA STRUCTURES ----
struct Planet {
	std::vector<struct Trait> planet_traits;
	int index;
};

// ---- HELPERS ----
template <typename T>
T random_choice(const std::vector<T>& vec) {
	std::uniform_int_distribution<> dist(0, (int)vec.size() - 1);
	return vec[dist(rng)];
}

template <typename T>
std::vector<T> random_sample(const std::vector<T>& vec, int count) {
	std::vector<T> copy = vec;
	std::shuffle(copy.begin(), copy.end(), rng);
	copy.resize(count);
	return copy;
}

// ---- ROUND SETUP ----
std::tuple<json, std::vector<std::string>, std::vector<std::string>> generate_creature(
	int human_trait_count, const json& data, const std::vector<std::string>& categories
) {
	auto chosen_categories = random_sample(categories, human_trait_count);
	json creature;

	for (const auto& cat : chosen_categories) {
		std::vector<std::string> traits;
		for (auto& [key, _] : data["creatures"][cat].items()) {
			traits.push_back(key);
		}

		creature[cat] = random_choice(traits);
	}

	std::vector<std::string> leftover_categories;
	for (const auto& c : categories) {
		if (std::find(chosen_categories.begin(), chosen_categories.end(), c) ==
			chosen_categories.end()) {
			leftover_categories.push_back(c);
		}
	}

	return { creature, chosen_categories, leftover_categories };
}

std::tuple<std::vector<Trait>, std::vector<Trait>, std::vector<Trait>> generate_trait_list(
	const json& data, const json& creature, const std::vector<std::string>& chosen_categories,
	const std::vector<std::string>& leftover_categories
) {
	std::vector<Trait> good_traits, bad_traits, random_traits;

	for (const auto& cat : chosen_categories) {
		std::string human_trait = creature[cat];

		for (const auto& pt : data["creatures"][cat][human_trait]["bad"]) {
			bad_traits.push_back({ pt, TraitQuality::BAD, cat });
		}

		for (const auto& pt : data["creatures"][cat][human_trait]["good"]) {
			good_traits.push_back({ pt, TraitQuality::GOOD, cat });
		}
	}

	for (const auto& cat : leftover_categories) {
		for (auto& [ht, value] : data["creatures"][cat].items()) {
			std::vector<std::string> combined;

			for (const auto& b : value["bad"]) {
				combined.push_back(b);
			}
			for (const auto& g : value["good"]) {
				combined.push_back(g);
			}

			assert(!combined.empty());

			for (const auto& pt : combined) {
				random_traits.push_back({ pt, TraitQuality::RANDOM, cat });
			}
		}
	}

	return { good_traits, bad_traits, random_traits };
}

// ---- PLANET GENERATION ----
Planet generate_planet(
	int planet_trait_count, std::vector<Trait> good_traits, std::vector<Trait> bad_traits,
	std::vector<Trait> random_traits, int leftover_category_count, std::pair<int, int> count,
	int index
) {
	auto [good_count, bad_count] = count;
	std::vector<Trait> planet_traits;

	// GOOD
	for (int i = 0; i < good_count; ++i) {
		if (good_traits.empty()) {
			PTGN_LOG(
				"Not enough good traits available! Asked for: ", good_count, " but only had: ", i
			);
			break;
		}
		auto choice = random_choice(good_traits);
		planet_traits.push_back(choice);

		good_traits.erase(
			std::remove_if(
				good_traits.begin(), good_traits.end(),
				[&](const Trait& t) { return t.category == choice.category; }
			),
			good_traits.end()
		);

		bad_traits.erase(
			std::remove_if(
				bad_traits.begin(), bad_traits.end(),
				[&](const Trait& t) { return t.category == choice.category; }
			),
			bad_traits.end()
		);
	}

	int needed_random_traits = planet_trait_count - (good_count + bad_count);

	PTGN_ASSERT(needed_random_traits >= 0, "More planet traits demanded than are available");

	// BAD
	for (int i = 0; i < bad_count; ++i) {
		if (bad_traits.empty()) {
			needed_random_traits += bad_count - i;
			break;
		}

		auto choice = random_choice(bad_traits);
		planet_traits.push_back(choice);

		bad_traits.erase(
			std::remove_if(
				bad_traits.begin(), bad_traits.end(),
				[&](const Trait& t) { return t.category == choice.category; }
			),
			bad_traits.end()
		);
	}

	if (leftover_category_count < needed_random_traits) {
		PTGN_LOG(
			"Warning: not enough random traits, requested ", needed_random_traits,
			" but only have ", leftover_category_count
		);
	}

	// RANDOM
	for (int i = 0; i < needed_random_traits; ++i) {
		if (random_traits.empty()) {
			break;
		}

		auto choice = random_choice(random_traits);
		planet_traits.push_back(choice);

		random_traits.erase(
			std::remove_if(
				random_traits.begin(), random_traits.end(),
				[&](const Trait& t) { return t.category == choice.category; }
			),
			random_traits.end()
		);
	}

	return { planet_traits, index };
}

struct DiceRoll {
	int winner_planet_index{ 0 };
	std::vector<Planet> planets;
	json chosen_human;
	std::vector<std::string> chosen_categories;
};

DiceRoll RollRoundDice(const json& trait_data, const Entry& entry) {
	std::vector<std::string> categories;
	for (auto& [key, _] : trait_data["creatures"].items()) {
		categories.push_back(key);
	}

	auto [creature, chosen_categories, leftover_categories] =
		generate_creature(entry.human_trait_count, trait_data, categories);

	auto [good_traits, bad_traits, random_traits] =
		generate_trait_list(trait_data, creature, chosen_categories, leftover_categories);

	std::vector<Planet> planets;

	for (int i = 0; i < entry.patterns.size(); ++i) {
		PTGN_ASSERT(
			entry.planet_trait_count >= entry.patterns[i].first + entry.patterns[i].second,
			"More planet traits demanded than are available"
		);

		planets.push_back(generate_planet(
			entry.planet_trait_count, good_traits, bad_traits, random_traits,
			static_cast<int>(leftover_categories.size()), entry.patterns[i], i
		));
	}

	std::shuffle(planets.begin(), planets.end(), rng);

	int winner = 0;
	for (int i = 0; i < planets.size(); ++i) {
		if (planets[i].index == 0) {
			winner = i;
		}
	}

	return DiceRoll{ winner, planets, creature, chosen_categories };
}