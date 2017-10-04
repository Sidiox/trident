#ifndef _LEARNER_H
#define _LEARNER_H

#include <trident/ml/embeddings.h>
#include <trident/ml/graddebug.h>
#include <trident/utils/batch.h>
#include <trident/kb/querier.h>

#include <tbb/concurrent_queue.h>

struct EntityGradient {
    const uint64_t id;
    uint32_t n;
    std::vector<float> dimensions;
    EntityGradient(const uint64_t id, uint16_t ndims) : id(id) {
        dimensions.resize(ndims);
        n = 0;
    }
};

struct BatchIO {
    //Input
    uint16_t epoch;
    const uint16_t batchsize;
    const uint16_t dims;
    std::vector<uint64_t> field1;
    std::vector<uint64_t> field2;
    std::vector<uint64_t> field3;
    std::vector<std::unique_ptr<float>> posSignMatrix;
    std::vector<std::unique_ptr<float>> neg1SignMatrix;
    std::vector<std::unique_ptr<float>> neg2SignMatrix;
    Querier *q;
    //Output
    uint64_t violations;
    uint64_t conflicts;

    BatchIO(uint16_t batchsize, const uint16_t dims) : batchsize(batchsize), dims(dims) {
        for(uint16_t i = 0; i < batchsize; ++i) {
            posSignMatrix.push_back(std::unique_ptr<float>(new float[dims]));
            neg1SignMatrix.push_back(std::unique_ptr<float>(new float[dims]));
            neg2SignMatrix.push_back(std::unique_ptr<float>(new float[dims]));
        }
        clear();
    }

    void clear() {
        epoch = conflicts = violations = 0;
        for(uint16_t i = 0; i < batchsize; ++i) {
            memset(posSignMatrix[i].get(), 0, sizeof(float) * dims);
            memset(neg1SignMatrix[i].get(), 0, sizeof(float) * dims);
            memset(neg2SignMatrix[i].get(), 0, sizeof(float) * dims);
        }
    }
};

struct ThreadOutput {
    uint64_t violations;
    uint64_t conflicts;
    ThreadOutput() {
        violations = 0;
        conflicts = 0;
    }
};

struct LearnParams {
    uint16_t epochs;
    uint32_t ne;
    uint32_t nr;
    uint16_t dim;
    float margin;
    float learningrate;
    uint16_t batchsize;
    bool adagrad;;
    uint16_t nthreads;
    uint16_t nstorethreads;
    uint32_t evalits;
    uint32_t storeits;
    std::string storefolder;
    bool compressstorage;
    std::string filetrace;
    float valid;
    float test;
    uint32_t numneg;
    bool feedback;
    uint32_t feedback_threshold;
    uint32_t feedback_minFullEpochs;

    std::unique_ptr<GradTracer> gradDebugger;

    std::string tostring() {
        std::string out = "";
        out += " epochs=" + to_string(epochs);
        out += " ne=" + to_string(ne);
        out += " nr=" + to_string(nr);
        out += " dim=" + to_string(dim);
        out += " margin=" + to_string(margin);
        out += " learningrate=" + to_string(learningrate);
        out += " batchsize=" + to_string(batchsize);
        out += " adagrad=" + to_string(adagrad);
        out += " nthreads=" + to_string(nthreads);
        out += " nstorethreads=" + to_string(nstorethreads);
        out += " evalits=" + to_string(evalits);
        out += " storeits=" + to_string(storeits);
        out += " storefolder=" + storefolder;
        out += " compresstorage=" + to_string(compressstorage);
        out += " filetrace=" + filetrace;
        out += " valid=" + to_string(valid);
        out += " test=" + to_string(test);
        out += " numneg=" + to_string(numneg);
        out += " feedbacks=" + to_string(feedback);
        out += " feedbacks_threshold=" + to_string(feedback_threshold);
        out += " feedbacks_minfullepoch=" + to_string(feedback_minFullEpochs);
        return out;
    }
};

class Learner {
    protected:
        KB &kb;
        const uint16_t epochs;
        const uint32_t ne;
        const uint32_t nr;
        const uint16_t dim;
        const float margin;
        const float learningrate;
        const uint16_t batchsize;
        const bool adagrad;

        std::shared_ptr<Embeddings<double>> E;
        std::shared_ptr<Embeddings<double>> R;
        std::unique_ptr<double> pe2; //used for adagrad
        std::unique_ptr<double> pr2; //used for adagrad

        //debugger
        std::unique_ptr<GradTracer> gradDebugger;

        float dist_l1(double* head, double* rel, double* tail,
                float *matrix);

        void batch_processer(
                Querier *q,
                tbb::concurrent_bounded_queue<std::shared_ptr<BatchIO>> *inputQueue,
                tbb::concurrent_bounded_queue<std::shared_ptr<BatchIO>> *outputQueue,
                ThreadOutput *output,
                uint16_t epoch);

        void update_gradients(BatchIO &io,
                std::vector<EntityGradient> &ge,
                std::vector<EntityGradient> &gr);

        //Store E and R into a file
        void store_model(string pathmodel, const bool gzipcompress,
                const uint16_t nthreads);

    public:
        Learner(KB &kb, LearnParams &p) :
            kb(kb), epochs(p.epochs), ne(p.ne), nr(p.nr), dim(p.dim), margin(p.margin),
            learningrate(p.learningrate), batchsize(p.batchsize), adagrad(p.adagrad),
            gradDebugger(std::move(p.gradDebugger)) {
            }


        void setup(const uint16_t nthreads);

        void setup(const uint16_t nthreads,
                std::shared_ptr<Embeddings<double>> E,
                std::shared_ptr<Embeddings<double>> R,
                std::unique_ptr<double> pe,
                std::unique_ptr<double> pr);

        void train(BatchCreator &batcher, const uint16_t nthreads,
                const uint16_t nstorethreads,
                const uint32_t evalits,
                const uint32_t storeits,
                const string pathvalid,
                const string storefolder,
                const bool compressstorage);

        virtual void process_batch(BatchIO &io,
                const uint16_t epoch,
                const uint16_t
                nbatches) = 0;

        //Load the model (=two sets of embeddings, E and R) from disk
        static std::pair<std::shared_ptr<Embeddings<double>>,
            std::shared_ptr<Embeddings<double>>>
                loadModel(string path);

        std::shared_ptr<Embeddings<double>> getE() {
            return E;
        }

        std::shared_ptr<Embeddings<double>> getR() {
            return R;
        }

        void getDebugger(std::unique_ptr<GradTracer> &debugger) {
            debugger = std::move(this->gradDebugger);
        }

        static void launchLearning(KB &kb, string op, LearnParams &p);
};
#endif