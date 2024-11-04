#include <array>
#include <iostream>
#include <regex>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

    const size_t NUM_MEDALS = 3;

    using score_t = int_least64_t;
    using medal_type_t = int_least8_t;
    using medal_array_t = std::array<score_t, NUM_MEDALS>;
    using country_ids_t = std::unordered_map<std::string, size_t>;
    using medals_count_t = std::vector<medal_array_t>;
    using country_names_t = std::vector<std::string>;

    const std::string country_name_regex_str = "[A-Z][ A-Za-z]*[A-Za-z]";

    // Creates a regex pattern that matches numbers less than or equal to n.
    // The matched numbers cannot have leading zeros, the only exception being
    // zero itself (if include_zero is true).
    // @param n: Maximum number to match.
    // @param include_zero: If true, the pattern will match zero as well.
    std::string create_regex_number_less_or_equal_n(size_t n, bool include_zero) {
        // Include the number n itself and optionally zero.
        std::string result = (include_zero ? "0|" : "") + std::to_string(n);
        
        size_t num_digits = 0; // Number of digits that have been removed from n.
        while (n > 0) {
            size_t last_digit = n % 10;
            n /= 10; // Remove the last digit from n.
            if ((last_digit > 0 && n > 0) || last_digit > 1)  {
                // Include all numbers that match some prefix of n
                // and then have a digit less than the corresponding
                // digit in n.
                result += "|";
                if (n > 0) {
                    result += std::to_string(n);
                    result += "[0-" + std::to_string(last_digit - 1) + "]";
                } else {
                    result += "[1-" + std::to_string(last_digit - 1) + "]";
                }
                if (num_digits > 0)
                    result += "[0-9]{" + std::to_string(num_digits) + "}";
            }
            num_digits++;
        }

        // Include all numbers that have fewer digits than n.
        if (num_digits > 2)
            result += "|[1-9][0-9]{0," + std::to_string(num_digits - 2) + "}";
        else if (num_digits == 2)
            result += "|[1-9]";

        return result;
    }

    // Creates a regex pattern that matches a query instruction.
    std::string create_query_pattern_str(size_t num_medals) {
        std::string result = "^=";
        for (size_t i = 0; i < NUM_MEDALS; i++)
            result += std::string("([1-9][0-9]{0,5})") + 
                                ((i < num_medals - 1) ? " " : "$");
        return result;
    }

    // Processes an instruction to add a medal to a country.
    // @param line: Instruction to process
    // @param country_ids: Maps country names to ids.
    // @param medals: Number of medals per country.
    // @param country_names: Names of countries.
    void add_medal(const std::string &country_name,
                   const medal_type_t medal_type,
                   country_ids_t &country_ids,
                   medals_count_t &medals,
                   country_names_t &country_names) {
        auto [it, inserted] = country_ids.insert({country_name, 
                                                country_names.size()});
        if (inserted) {
            medals.push_back(medal_array_t{});
            country_names.push_back(country_name);
        }
            
        size_t country_id = it->second;
        if (medal_type > 0)
            medals[country_id][medal_type - 1]++;
    }

    // Processes an instruction to remove a medal from a country.
    // @param line: Instruction to process
    // @param country_ids: Maps country names to ids.
    // @param medals: Number of medals per country.
    // @return: True if the instruction was processed successfully, false otherwise.
    bool remove_medal(const std::string &country_name,
                      const medal_type_t medal_type,
                      const country_ids_t &country_ids,
                      medals_count_t &medals) {
        auto it = country_ids.find(country_name);
        if (it == country_ids.end()) // Country does not exist in the table.
            return false;

        size_t country_id = it->second;
        if (medals[country_id][medal_type - 1] == 0)
            // Country does not have the medal.
            return false;
        medals[country_id][medal_type - 1]--;
        return true;
    }

    // Processes an instruction to query the medal table.
    // @param line: Instruction to process
    // @param medals: Number of medals per country.
    // @param country_names: Names of countries.
    void print_ranking(const medal_array_t &weights,
                       const medals_count_t &medals,
                       const country_names_t &country_names) {
        
        // Use a static vector to avoid redundant allocations.
        static std::vector<std::pair<score_t, std::string>> scores_and_countries;
        scores_and_countries.resize(medals.size());
        
        for (size_t i = 0; i < medals.size(); i++) {
            score_t score = 0;
            for (size_t j = 0; j < NUM_MEDALS; j++)
                score += medals[i][j] * weights[j];
            scores_and_countries[i] = {score, country_names[i]};
        }

        // Sort the countries by their scores in non-increasing order.
        // In case of ties, the countries are sorted lexicographically.
        sort(scores_and_countries.begin(), scores_and_countries.end(), 
            [](const std::pair<score_t, std::string> &a, 
               const std::pair<score_t, std::string> &b) {
            return a.first > b.first || 
                        (a.first == b.first && a.second < b.second);
        });

        // Print the countries sorted by their scores.
        // If two countries have the same score, they are assigned the same rank.
        score_t prev_score = 0;
        size_t rank = 1;
        for (size_t i = 0; i < scores_and_countries.size(); i++) {
            if (scores_and_countries[i].first != prev_score) {
                rank = i + 1;
            }
            std::cout << rank << ". " << 
                            scores_and_countries[i].second << std::endl;
            prev_score = scores_and_countries[i].first;
        }
    }
}


int main() {
    
    std::string add_medal_pattern_str = "^(" + country_name_regex_str +
            ") (" + create_regex_number_less_or_equal_n(NUM_MEDALS, true) + ")$";
    std::string remove_medal_pattern_str = "^-(" + country_name_regex_str +
            ") (" + create_regex_number_less_or_equal_n(NUM_MEDALS, false) + ")$";
    std::string query_pattern_str = create_query_pattern_str(NUM_MEDALS);

    // Regex patterns for the three types of instructions
    const std::regex add_medal_pattern{
        add_medal_pattern_str, std::regex::optimize
    };
    const std::regex remove_medal_pattern{
        remove_medal_pattern_str, std::regex::optimize
    };
    const std::regex query_pattern{
        query_pattern_str, std::regex::optimize
    };

    std::string line;
    country_ids_t country_ids; // Maps country names to ids
    medals_count_t medals; // Number of medals per country
    country_names_t country_names; // Names of countries

    for (size_t line_num = 1; getline(std::cin, line); line_num++) {
        bool error = false;

        std::smatch match;

        if (std::regex_match(line, match, add_medal_pattern)) {
            std::string country_name = match[1].str();
            medal_type_t medal_type = std::stoi(match[2].str());
            add_medal(country_name, medal_type, country_ids, medals, country_names);
        } else if (std::regex_match(line, match, remove_medal_pattern)) {
            std::string country_name = match[1].str();
            medal_type_t medal_type = std::stoi(match[2].str());
            error |= !remove_medal(country_name, medal_type, country_ids, medals);
        } else if (std::regex_match(line, match, query_pattern)) {
            medal_array_t weights;
            for (size_t i = 0; i < NUM_MEDALS; i++)
                weights[i] = std::stoi(match[i + 1].str());
            print_ranking(weights, medals, country_names);
        } else {
            error = true;
        }

        if (error)
            std::cerr << "ERROR " << line_num << std::endl;
    }

    return 0;
}