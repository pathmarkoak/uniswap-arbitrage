#include <iostream>
#include <fstream>
#include <vector>
#include <map>
#include <algorithm>
#include <chrono>
#include <thread>
#include "thread.h"
#include "common.h"
#include "dfs.h"
#include "events.h"

using namespace std;

json all_pairs = loadJSON("files/pairs.json");
vector<pair<string, json>> pairs;
map<string, json> pairsDict;
json startToken;
json tokenIn;
json tokenOut;
vector<string> currentPairs;
vector<string> path;
vector<json> bestTrades;

struct CurrencyPair {
    std::string source;
    std::string destination;
    double rate;
};

class Graph {
private:
    std::vector<CurrencyPair> pairs;
    std::map<std::string, double> dist;

public:
    void addPair(const CurrencyPair& pair) {
        pairs.push_back(pair);
    }

    void initializeDistances(const std::string& source) {
        for (const auto& pair : pairs) {
            dist[pair.source] = std::numeric_limits<double>::infinity();
            dist[pair.destination] = std::numeric_limits<double>::infinity();
        }
        dist[source] = 0.0;
    }

    void bellmanFord(const std::string& source) {
        initializeDistances(source);
        size_t V = dist.size();

        for (size_t i = 0; i < V - 1; ++i) {
            for (const auto& pair : pairs) {
                if (dist[pair.source] + pair.rate < dist[pair.destination]) {
                    dist[pair.destination] = dist[pair.source] + pair.rate;
                }
            }
        }

        // Check for negative-weight cycles
        for (const auto& pair : pairs) {
            if (dist[pair.source] + pair.rate < dist[pair.destination]) {
                std::cout << "Arbitrage opportunity detected!" << std::endl;
                // Trade logic goes here, trader-dependent
            }
        }
    }
};

int main() {
    Graph graph;
    CurrencyPair pair1 = {"USD", "EUR", 0.85}; // Example pair
    graph.addPair(pair1);

    std::string startCurrency = "USD"; // Example start currency
    graph.bellmanFord(startCurrency);

    return 0;
}

void printMoney(int amountIn, json p, int gasPrice, int profit) {
    int deadline = time(0) + 600;
    auto tx = printer.functions.printMoney(startToken["address"], amountIn, amountIn, p, deadline).buildTransaction({
        {"from", address},
        {"value", 0},
        {"gasPrice", gasPrice},
        {"gas", 1500000},
        {"nonce", w3.eth.getTransactionCount(address)},
    });
    try {
        int gasEstimate = w3.eth.estimateGas(tx);
        cout << "estimate gas cost: " << gasEstimate * gasPrice / 1e18 << endl;
    } catch (const exception &e) {
        cout << "gas estimate err: " << e.what() << endl;
        return;
    }
    if (config["start"] == "usdt" || config["start"] == "usdc" || config["start"] == "dai") {
        if (gasEstimate * gasPrice / 1e18 * 360 >= profit / pow(10, startToken["decimal"])) {
            cout << "gas too much, give up..." << endl;
            return;
        }
    }
    if (config["start"] == "weth" && gasEstimate * gasPrice >= profit) {
        cout << "gas too much, give up..." << endl;
        return;
    }
    auto signed_tx = w3.eth.account.sign_transaction(tx, private_key=privkey);
    try {
        string txhash = w3.eth.sendRawTransaction(signed_tx.rawTransaction);
        return txhash;
    } catch (const exception &e) {
        cout << "sendRawTransaction error: " << e.what() << endl;
        return;
    }
}

void flashPrintMoney(int amountIn, json p, int gasPrice, int profit) {
    auto tx = printer.functions.flashPrintMoney(startToken["address"], amountIn, p).buildTransaction({
        {"from", address},
        {"value", 0},
        {"gasPrice", gasPrice},
        {"gas", 1500000},
        {"nonce", w3.eth.getTransactionCount(address)},
    });
    try {
        int gasEstimate = w3.eth.estimateGas(tx);
        cout << "estimate gas cost: " << gasEstimate * gasPrice / 1e18 << endl;
    } catch (const exception &e) {
        cout << "gas estimate err: " << e.what() << endl;
        return;
    }
    if (config["start"] == "usdt" || config["start"] == "usdc" || config["start"] == "dai") {
        if (gasEstimate * gasPrice / 1e18 * 360 >= profit / pow(10, startToken["decimal"])) {
            cout << "gas too much, give up..." << endl;
            return;
        }
    }
    if (config["start"] == "weth" && gasEstimate * gasPrice >= profit) {
        cout << "gas too much, give up..." << endl;
        return;
    }
    auto signed_tx = w3.eth.account.sign_transaction(tx, private_key=privkey);
    try {
        string txhash = w3.eth.sendRawTransaction(signed_tx.rawTransaction);
        return txhash;
    } catch (const exception &e) {
        cout << "sendRawTransaction error: " << e.what() << endl;
        return;
    }
}

void doTrade(int balance, json trade) {
    vector<string> p;
    for (const auto &t : trade["path"]) {
        p.push_back(t["address"]);
    }
    int amountIn = int(trade["optimalAmount"]);
    bool useFlash = false;
    if (amountIn > balance) {
        useFlash = true;
    }
    int minOut = int(amountIn);
    string to = config["address"];
    int deadline = time(0) + 600;
    cout << amountIn << " " << minOut << " " << p << " " << to << " " << deadline << endl;
    try {
        // auto amountsOut = uni.get_amounts_out(amountIn, p);
        auto amountsOut = vector<int>{int(trade["outputAmount"])};
        cout << "amountsOut: " << amountsOut << endl;
    } catch (const exception &e) {
        cout << "exception: " << e.what() << endl;
        return;
    }
    if (amountsOut.back() > amountIn) {
        int gasPrice = int(gasnow()["rapid"] * 1.2);
        if (useFlash) {
            txhash = flashPrintMoney(amountIn, p, gasPrice, amountsOut.back() - amountIn);
        } else {
            txhash = printMoney(amountIn, p, gasPrice, amountsOut.back() - amountIn);
        }
        return txhash;
    }
    return;
}

bool needChangeKey = false;
vector<json> get_reserves_batch_mt(vector<pair<string, json>> pairs) {
    if (pairs.size() <= 200) {
        return get_reserves(pairs);
    } else {
        size_t s = 0;
        vector<thread> threads;
        while (s < pairs.size()) {
            size_t e = s + 200;
            if (e > pairs.size()) {
                e = pairs.size();
            }
            threads.push_back(thread(get_reserves, pairs.begin() + s, pairs.begin() + e));
            s = e;
        }
        for (auto &t : threads) {
            t.join();
        }
        vector<json> new_pairs;
        for (auto &t : threads) {
            auto ret = t.get_result();
            if (!ret.empty()) {
                needChangeKey = true;
            }
            new_pairs.insert(new_pairs.end(), ret.begin(), ret.end());
        }
        return new_pairs;
    }
}

int last_key = 0;

void main() {
    if (config["pairs"] == "random") {
        auto result = selectPairs(all_pairs);
        pairs = result.first;
        pairsDict = result.second;
    }
    cout << "pairs: " << pairs.size() << endl;
    try {
        pairs = get_reserves_batch_mt(pairs);
        if (needChangeKey) {
            needChangeKey = false;
            size_t l = config["https"].size();
            string http_addr = config["https"][(last_key + 1) % l];
            last_key += 1;
            last_key %= l;
            uni = UniswapV2Client(address, privkey, http_addr);
            w3
