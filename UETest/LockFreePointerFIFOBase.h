#include <atomic>

template<typename T>
class FLockFreePointerFIFOBase
{
private:
	struct Node
	{
		T* data;
		std::atomic<Node*> next;

		Node(T* data) : data(data), next(nullptr) {}
	};

	std::atomic<Node*> head;
	std::atomic<Node*> tail;

public:
	FLockFreePointerFIFOBase() : head(nullptr), tail(nullptr) {}

	~FLockFreePointerFIFOBase()
	{
		while (Node* node = head.load())
		{
			head.store(node->next);
			delete node->data;
			delete node;
		}
	}

	void Enqueue(T* data)
	{
		Node* newNode = new Node(data);
		newNode->next.store(nullptr);

		Node* curTail = tail.exchange(newNode);

		if (curTail)
		{
			curTail->next.store(newNode);
		}
		else
		{
			head.store(newNode);
		}
	}

	T* Dequeue()
	{
		Node* curHead = head.load();
		
		while (curHead)
		{
			Node* nextNode = curHead->next.load();

			if (head.compare_exchange_weak(curHead, nextNode))
			{
				Node* curTail = curHead;
				tail.compare_exchange_strong(curTail, nullptr);
				
				T* data = curHead->data;
				delete curHead;
				return data;
			}
		}

		return nullptr; // ╤сапн╙©у
	}
};