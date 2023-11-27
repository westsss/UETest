#include <atomic>
#include <windows.h>

template<typename T>
class FLockFreePointerFIFOBase
{
private:
	struct Node
	{
		T* data;
		Node* next;

		Node(T* data) : data(data), next(nullptr) {}
	};

	Node* head;
	Node* tail;

	CRITICAL_SECTION cs; // 创建一个 Critical Section

public:
	FLockFreePointerFIFOBase() : head(nullptr), tail(nullptr) 
	{
		InitializeCriticalSection(&cs); // 初始化临界区
	}

	~FLockFreePointerFIFOBase()
	{
		while (Node* node = head)
		{
			head = node->next;
			delete node->data;
			delete node;
		}

		DeleteCriticalSection(&cs); // 销毁临界区
	}

	void Enqueue(T* data)
	{
		Node* newNode = new Node(data);
		newNode->next = nullptr;

		EnterCriticalSection(&cs); // 进入临界区

		if (tail)
		{
			tail->next = newNode;
		}
		else
		{
			head = newNode;
		}

		tail = newNode;

		LeaveCriticalSection(&cs); // 离开临界区
	}

	T* Dequeue()
	{
		T* data = nullptr;

		EnterCriticalSection(&cs); // 进入临界区

		if (head)
		{
			data = head->data;
			head = head->next;
		}

		if (head == nullptr)
		{
			tail = nullptr;
		}

		LeaveCriticalSection(&cs); // 离开临界区

		return data; // 队列为空
	}
};