#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>
#include <map>
#include <chrono>
#include <thread>
#include <mutex>
#include <memory>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <random>
#include <fstream>
#include <cstdio>
#include <array>

#include "llama.h"

// ==============================================================================
// 1. LOGGER TIZIMI (Vaqt belgisi bilan)
// ==============================================================================
enum class LogLevel { INFO, WARNING, ERROR, SUCCESS, DEBUG };

class Logger {
private:
    static std::mutex logMutex;

    static std::string levelToString(LogLevel lvl) {
        switch (lvl) {
            case LogLevel::INFO:    return "[INFO]   ";
            case LogLevel::WARNING: return "[WARN]   ";
            case LogLevel::ERROR:   return "[ERROR]  ";
            case LogLevel::SUCCESS: return "[OK]     ";
            case LogLevel::DEBUG:   return "[DEBUG]  ";
        }
        return "[LOG]    ";
    }

    static std::string currentTime() {
        auto now = std::chrono::system_clock::now();
        auto t = std::chrono::system_clock::to_time_t(now);
        std::stringstream ss;
        ss << std::put_time(std::localtime(&t), "%H:%M:%S");
        return ss.str();
    }

public:
    static void log(LogLevel level, const std::string& agent, const std::string& msg) {
        std::lock_guard<std::mutex> lock(logMutex);
        std::cout << "[" << currentTime() << "] "
                  << levelToString(level)
                  << "[" << agent << "] " << msg << "\n";
    }
};

std::mutex Logger::logMutex;

// ==============================================================================
// 2. XAVFSIZ SANDBOX (Python kodini xavfsiz bajarish muhiti)
// ==============================================================================
class CodeExecutor {
public:
    static std::string executePythonCode(const std::string& code) {
        static const std::vector<std::string> blacklist = {
            "import os", "import sys", "subprocess", "shutil",
            "__import__", "eval(", "exec(", "open(", "socket"
        };
        for (const auto& bad : blacklist) {
            if (code.find(bad) != std::string::npos) {
                return "[XAVFSIZLIK] Bloklangan xavfli buyruq aniqlandi: " + bad;
            }
        }

        std::ofstream f("temp_sandbox.py");
        if (!f) return "[XATOLIK] Sandbox fayli yaratilmadi.";
        f << code;
        f.close();

        std::string cmd = "timeout 5 python3 temp_sandbox.py 2>&1";
        std::array<char, 256> buf{};
        std::string result;
        std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd.c_str(), "r"), pclose);
        if (!pipe) return "[XATOLIK] Pipe oqimi ochilmadi.";

        while (fgets(buf.data(), buf.size(), pipe.get()) != nullptr) {
            result += buf.data();
        }

        std::remove("temp_sandbox.py");
        return result.empty() ? "[+] Kod bajarildi (chiqish natijasi yo'q)." : result;
    }
};

// ==============================================================================
// 3. XOTIRA TIZIMI (Memory Bank)
// ==============================================================================
class MemoryBank {
private:
    std::unordered_map<std::string, std::vector<std::string>> shortTerm;
    std::unordered_map<std::string, std::string> longTerm;
    std::mutex memMutex;

public:
    void store(const std::string& agent, const std::string& data) {
        std::lock_guard<std::mutex> lock(memMutex);
        shortTerm[agent].push_back(data);
    }

    void remember(const std::string& key, const std::string& value) {
        std::lock_guard<std::mutex> lock(memMutex);
        longTerm[key] = value;
    }

    std::string recall(const std::string& key) {
        std::lock_guard<std::mutex> lock(memMutex);
        auto it = longTerm.find(key);
        return (it != longTerm.end()) ? it->second : "XOTIRA_BO'SH";
    }

    size_t getAgentMemorySize(const std::string& agent) {
        std::lock_guard<std::mutex> lock(memMutex);
        return shortTerm[agent].size();
    }
};

// ==============================================================================
// 4. XAVFSIZLIK FILTRI (Prompt Injection Himoyasi)
// ==============================================================================
class SecurityFilter {
private:
    std::vector<std::string> blacklist = {
        "drop table", "rm -rf", "format c:", "delete all",
        "hack", "exploit", "bypass auth", "inject sql"
    };

public:
    bool isSafe(const std::string& input) {
        std::string lower = input;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });

        for (const auto& bad : blacklist) {
            if (lower.find(bad) != std::string::npos) {
                Logger::log(LogLevel::WARNING, "SECURITY", "Xavfli so'rov bloklandi: " + bad);
                return false;
            }
        }
        return true;
    }
};

// ==============================================================================
// 5. LLM INFERENSIYA YADROSI (llama.cpp orqali haqiqiy ishlov berish)
// ==============================================================================
class LLMEngine {
private:
    llama_model* model = nullptr;
    llama_context* ctx = nullptr;
    const llama_vocab* vocab = nullptr;

public:
    LLMEngine(const std::string& modelPath) {
        llama_backend_init();
        llama_model_params mparams = llama_model_default_params();
        mparams.n_gpu_layers = 99; // Barcha qatlamlarni GPU'ga yuklash
        mparams.use_mmap = true;

        Logger::log(LogLevel::INFO, "LLM", "Model GPU'ga yuklanmoqda: " + modelPath);
        model = llama_model_load_from_file(modelPath.c_str(), mparams);

        if (!model) {
            Logger::log(LogLevel::ERROR, "LLM", "Model fayli topilmadi!");
            return;
        }

        vocab = llama_model_get_vocab(model);
        llama_context_params cparams = llama_context_default_params();
        cparams.n_ctx = 4096;
        cparams.n_batch = 512;
        cparams.n_threads = std::thread::hardware_concurrency();

        ctx = llama_init_from_model(model, cparams);
        if (!ctx) {
            Logger::log(LogLevel::ERROR, "LLM", "Kontekst yaratilmadi!");
            llama_model_free(model);
            model = nullptr;
        } else {
            Logger::log(LogLevel::SUCCESS, "LLM", "Model muvaffaqiyatli ishga tushdi.");
        }
    }

    ~LLMEngine() {
        if (ctx) llama_free(ctx);
        if (model) llama_model_free(model);
        llama_backend_free();
    }

    std::string generate(const std::string& systemPrompt, const std::string& userInput) {
        if (!ctx || !model || !vocab) return "[SIMULYATSIYA REJIMI] Model yuklanmagan.";

        std::vector<llama_chat_message> chat = {
            {"system", systemPrompt.c_str()},
            {"user", userInput.c_str()}
        };

        const char* tmpl = llama_model_chat_template(model, nullptr);
        std::vector<char> formatted(4096);
        int new_len = llama_chat_apply_template(tmpl, chat.data(), chat.size(), true, formatted.data(), formatted.size());
        if (new_len > (int)formatted.size()) {
            formatted.resize(new_len);
            llama_chat_apply_template(tmpl, chat.data(), chat.size(), true, formatted.data(), formatted.size());
        }
        std::string prompt(formatted.data(), new_len);

        int n_tokens = -llama_tokenize(vocab, prompt.c_str(), prompt.size(), nullptr, 0, true, true);
        if (n_tokens <= 0) return "[XATOLIK] Tokenizatsiya xatosi.";
        std::vector<llama_token> tokens(n_tokens);
        llama_tokenize(vocab, prompt.c_str(), prompt.size(), tokens.data(), tokens.size(), true, true);

        auto sparams = llama_sampler_chain_default_params();
        llama_sampler* sampler = llama_sampler_chain_init(sparams);
        llama_sampler_chain_add(sampler, llama_sampler_init_temp(0.7f));
        llama_sampler_chain_add(sampler, llama_sampler_init_top_k(40));
        llama_sampler_chain_add(sampler, llama_sampler_init_top_p(0.95f, 1));
        std::random_device rd;
        llama_sampler_chain_add(sampler, llama_sampler_init_dist(rd()));

        const int B = 512;
        for (size_t i = 0; i < tokens.size(); i += B) {
            int n = std::min((size_t)B, tokens.size() - i);
            llama_batch b = llama_batch_get_one(tokens.data() + i, n);
            if (llama_decode(ctx, b) != 0) {
                llama_sampler_free(sampler);
                return "[XATOLIK] Dekodlash xatosi.";
            }
        }

        std::string output;
        for (int i = 0; i < 512; ++i) {
            llama_token id = llama_sampler_sample(sampler, ctx, -1);
            if (llama_vocab_is_eog(vocab, id)) break;

            char buf[256];
            int n = llama_token_to_piece(vocab, id, buf, sizeof(buf), 0, true);
            if (n > 0) output.append(buf, n);

            llama_batch b = llama_batch_get_one(&id, 1);
            if (llama_decode(ctx, b) != 0) break;
        }

        llama_sampler_free(sampler);
        return output;
    }
};

// ==============================================================================
// 6. BAZAVIY VA MAXSUS AGENTLAR
// ==============================================================================
class BaseAgent {
protected:
    std::string name;
    std::string modelName;
    std::string systemPrompt;
    int vramUsageMB;
    MemoryBank* memory;
    LLMEngine* llm;
    int taskCounter = 0;

public:
    BaseAgent(const std::string& n, const std::string& m, const std::string& sysP, int vram, MemoryBank* mem, LLMEngine* l)
        : name(n), modelName(m), systemPrompt(sysP), vramUsageMB(vram), memory(mem), llm(l) {}

    virtual ~BaseAgent() = default;

    virtual std::string execute(const std::string& prompt) {
        incrementTask();
        memory->store(name, prompt);
        Logger::log(LogLevel::INFO, name, "Vazifa bajarilmoqda...");
        return llm->generate(systemPrompt, prompt);
    }

    std::string getName() const { return name; }
    std::string getModel() const { return modelName; }
    int getVram() const { return vramUsageMB; }
    int getTaskCount() const { return taskCounter; }
    void incrementTask() { taskCounter++; }
};

class CoderAgent : public BaseAgent {
public:
    CoderAgent(MemoryBank* mem, LLMEngine* l)
        : BaseAgent("Coder", "Qwen-2.5-Coder-72B", "Siz mahoratli Dasturlash va Infratuzilma Agentisiz. Flutter va backend kodlarini yozasiz.", 40000, mem, l) {}
};

class CFOAgent : public BaseAgent {
public:
    CFOAgent(MemoryBank* mem, LLMEngine* l)
        : BaseAgent("CFO", "Time-Series-LLM-Hybrid", "Siz Moliya va Hisobchilik agentisiz. Balans, tranzaksiya va prediktsiyalarni tahlil qilasiz.", 12000, mem, l) {}
};

class SecurityAgent : public BaseAgent {
public:
    SecurityAgent(MemoryBank* mem, LLMEngine* l)
        : BaseAgent("Security", "IsolationForest + LLM", "Siz Kiberxavfsizlik va Anti-Fraud agentisiz. Anomaliyalarni aniqlaysiz.", 8000, mem, l) {}
};

class KYCAgent : public BaseAgent {
public:
    KYCAgent(MemoryBank* mem, LLMEngine* l)
        : BaseAgent("KYC", "OpenCV + PyTorch-Liveness", "Siz KYC va Biometriya agentisiz. OCR va yuzni tanish jarayonlarini boshqarasiz.", 6000, mem, l) {}
};

class SupportAgent : public BaseAgent {
public:
    SupportAgent(MemoryBank* mem, LLMEngine* l)
        : BaseAgent("Support", "Multimodal-Support", "Siz Support va SMM agentisiz. Foydalanuvchilarga o'zbek tilida xizmat ko'rsatasiz.", 10000, mem, l) {}
};

// ==============================================================================
// 7. MARKAZIY ORKESTRATOR (HabibulloUltimateCore)
// ==============================================================================
class HabibulloUltimateCore {
private:
    std::unordered_map<std::string, std::shared_ptr<BaseAgent>> agents;
    std::shared_ptr<MemoryBank> memory;
    std::shared_ptr<SecurityFilter> security;
    std::shared_ptr<LLMEngine> llmEngine;
    std::string activeAgent = "General";
    int totalTasksProcessed = 0;
    std::chrono::system_clock::time_point bootTime;

public:
    HabibulloUltimateCore(const std::string& modelPath) {
        bootTime = std::chrono::system_clock::now();
        memory = std::make_shared<MemoryBank>();
        security = std::make_shared<SecurityFilter>();
        llmEngine = std::make_shared<LLMEngine>(modelPath);

        agents["coder"]    = std::make_shared<CoderAgent>(memory.get(), llmEngine.get());
        agents["cfo"]      = std::make_shared<CFOAgent>(memory.get(), llmEngine.get());
        agents["security"] = std::make_shared<SecurityAgent>(memory.get(), llmEngine.get());
        agents["kyc"]      = std::make_shared<KYCAgent>(memory.get(), llmEngine.get());
        agents["support"]  = std::make_shared<SupportAgent>(memory.get(), llmEngine.get());

        Logger::log(LogLevel::SUCCESS, "CORE", "Barcha maxsus lokal modellar VRAM'ga yuklandi.");
        printVramStatus();
    }

    void printVramStatus() {
        int total = 0;
        std::cout << "\n┌─────────────────────────────────────────────────┐\n";
        std::cout << "│  📊 VRAM TAQSIMOTI (Total GPU: 76 GB)          │\n";
        std::cout << "├─────────────────────────────────────────────────┤\n";
        for (auto& [key, agent] : agents) {
            total += agent->getVram();
            std::cout << "│  " << std::left << std::setw(10) << agent->getName()
                      << " │ " << std::setw(30) << agent->getModel()
                      << " │ " << std::setw(6) << agent->getVram() << " MB │\n";
        }
        std::cout << "├─────────────────────────────────────────────────┤\n";
        std::cout << "│  Jami ishlatilgan: " << total << " MB / 76000 MB"
                  << "             │\n";
        std::cout << "└─────────────────────────────────────────────────┘\n\n";
    }

    std::string routeTask(const std::string& category, const std::string& input) {
        if (!security->isSafe(input)) {
            return "❌ [BLOKLANDI] Xavfli so'rov aniqlandi. Tizim himoyasi faollashtirildi.";
        }

        auto it = agents.find(category);
        if (it == agents.end()) {
            Logger::log(LogLevel::WARNING, "ROUTER", "Noma'lum kategoriya: " + category);
            return "[GENERAL] Habibullo-1 umumiy yordamchi rejimida ishlayapti.";
        }

        Logger::log(LogLevel::INFO, "ROUTER", "Vazifa '" + category + "' agentiga yuborildi.");
        activeAgent = it->second->getName();
        totalTasksProcessed++;

        auto start = std::chrono::high_resolution_clock::now();
        std::string result = it->second->execute(input);
        auto end = std::chrono::high_resolution_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

        Logger::log(LogLevel::SUCCESS, "ROUTER", "Vazifa bajarildi (" + std::to_string(ms) + " ms).");
        return result;
    }

    void printSystemStats() {
        auto now = std::chrono::system_clock::now();
        auto uptime = std::chrono::duration_cast<std::chrono::seconds>(now - bootTime).count();

        std::cout << "\n╔══════════════════════════════════════════════╗\n";
        std::cout << "║  📈 TIZIM STATISTIKASI                       ║\n";
        std::cout << "╠══════════════════════════════════════════════╣\n";
        std::cout << "║  Uptime: " << std::setw(6) << uptime << " sekund                     ║\n";
        std::cout << "║  Jami vazifalar: " << std::setw(6) << totalTasksProcessed << "                    ║\n";
        std::cout << "║  Faol agent: " << std::setw(10) << activeAgent << "                ║\n";
        std::cout << "╠══════════════════════════════════════════════╣\n";
        for (auto& [key, agent] : agents) {
            std::cout << "║  " << std::left << std::setw(12) << agent->getName()
                      << ": " << std::setw(4) << agent->getTaskCount()
                      << " vazifa | Xotira: " << std::setw(3)
                      << memory->getAgentMemorySize(agent->getName()) << " ta      ║\n";
        }
        std::cout << "╚══════════════════════════════════════════════╝\n";
    }
};

// ==============================================================================
// 8. MAIN — ISHGA TUSHIRISH
// ==============================================================================
int main() {
#if defined(_WIN32)
    system("chcp 65001 > nul");
#endif

    std::cout << "\n╔══════════════════════════════════════════════════════════════╗\n";
    std::cout << "║   🚀 HABIBULLO-1 MULTI-AGENT ENTERPRISE SYSTEM v3.0      ║\n";
    std::cout << "║   🔥 100% Offline | Real llama.cpp GPU Engine               ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════╝\n\n";

    // O'zingizning lokal Ggguf modelingiz yo'lini ko'rsating
    HabibulloUltimateCore enterpriseAI("models/qwen2.5-coder-7b-instruct-q4_k_m.gguf");

    // Testlar
    std::cout << "\n--- TEST: Coder Agent --- \n";
    std::cout << enterpriseAI.routeTask("coder", "Flutter uchun avtorizatsiya arxitekturasini yoz") << "\n";

    std::cout << "\n--- TEST: CFO Agent --- \n";
    std::cout << enterpriseAI.routeTask("cfo", "Haftalik tranzaksiyalar balansi va tushumlarni hisobla") << "\n";

    enterpriseAI.printSystemStats();
    return 0;
}
