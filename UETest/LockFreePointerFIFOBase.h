#include <atomic>

template<typename T>
class FLockFreePointerFIFOBase
{
public:
	FLockFreePointerFIFOBase()
	{
		Node* dummy = new Node;
		m_Head.store(dummy);
		m_Tail.store(dummy);
	}

	~FLockFreePointerFIFOBase()
	{
		while (Node* node = m_Head.load())
		{
			m_Head.store(node->next);
			delete node;
		}
	}

	void Enqueue(T* data)
	{
		Node* newNode = new Node;
		newNode->data = data;
		newNode->next = nullptr;

		while (true)
		{
			Node* tail = m_Tail.load();
			Node* next = tail->next;

			if (tail == m_Tail.load())
			{
				if (next == nullptr)
				{
					if (std::atomic_compare_exchange_strong(&tail->next, &next, newNode))
					{
						std::atomic_compare_exchange_strong(&m_Tail, &tail, newNode);
						return;
					}
				}
				else
				{
					std::atomic_compare_exchange_strong(&m_Tail, &tail, next);
				}
			}
		}
	}

	T* Dequeue()
	{
		while (true)
		{
			Node* head = m_Head.load();
			Node* tail = m_Tail.load();
			Node* next = head->next;

			if (head == m_Head.load())
			{
				if (head == tail)
				{
					if (next == nullptr)
					{
						return nullptr;
					}

					std::atomic_compare_exchange_strong(&m_Tail, &tail, next);
				}
				else
				{
					T* data = next->data;

					if (std::atomic_compare_exchange_strong(&m_Head, &head, next))
					{
						delete head;
						return data;
					}
				}
			}
		}
	}

private:
	struct Node
	{
		T* data;
		std::atomic<Node*> next;
	};

	std::atomic<Node*> m_Head;
	std::atomic<Node*> m_Tail;
};