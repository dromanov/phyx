#pragma once

#include <vector>
#include <thread>
#include <functional>
#include <queue>
#include <mutex>
#include <condition_variable>

class WorkQueue
{
  public:
    struct Item
    {
        virtual ~Item() {}

        virtual void run(int worker) = 0;
    };

    static unsigned int getIdealWorkerCount()
    {
        return std::max(std::thread::hardware_concurrency(), 1u);
    }

    WorkQueue(size_t workerCount)
        : signalTriggered(false)
    {
        for (size_t i = 0; i < workerCount; ++i)
            workers.emplace_back(workerThreadFun, this, i);
    }

    ~WorkQueue()
    {
        for (size_t i = 0; i < workers.size(); ++i)
            push(std::unique_ptr<Item>());

        for (size_t i = 0; i < workers.size(); ++i)
            workers[i].join();
    }

    void push(std::unique_ptr<Item> item)
    {
        std::unique_lock<std::mutex> lock(itemsMutex);

        items.push(std::move(item));

        lock.unlock();
        itemsCondition.notify_one();
    }

    void push(std::function<void()> fun)
    {
        std::unique_ptr<ItemFunction> item(new ItemFunction());
        item->function = std::move(fun);

        push(std::move(item));
    }

    size_t getWorkerCount() const
    {
        return workers.size();
    }

    void signalWait()
    {
        std::unique_lock<std::mutex> lock(signalMutex);

        signalCondition.wait(lock, [&]()
                             { return signalTriggered;
                             });

        signalTriggered = false;
    }

    void signalReady()
    {
        signalTriggered = true;

        signalCondition.notify_one();
    }

  private:
    std::vector<std::thread> workers;

    std::mutex itemsMutex;
    std::condition_variable itemsCondition;
    std::queue<std::unique_ptr<Item>> items;

    std::mutex signalMutex;
    std::condition_variable signalCondition;
    bool signalTriggered;

    static void workerThreadFun(WorkQueue* queue, int worker)
    {
        for (;;)
        {
            std::unique_ptr<Item> item;

            {
                std::unique_lock<std::mutex> lock(queue->itemsMutex);

                queue->itemsCondition.wait(lock, [&]()
                                           { return !queue->items.empty();
                                           });

                item = std::move(queue->items.front());
                queue->items.pop();
            }

            if (!item) break;

            item->run(worker);
        }
    }

    struct ItemFunction : Item
    {
        std::function<void()> function;

        void run(int worker) override
        {
            function();
        }
    };
};