#include <atomic>
#include <windows.h>
#include <string>

extern HANDLE g_hConsole;

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
	int Count;

public:
	FLockFreePointerFIFOBase() : head(nullptr), tail(nullptr), Count(0)
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

	int GetCount()
	{
		return Count;
	}

	void Enqueue(T* data)
	{
		Node* newNode = new Node(data);
		newNode->next = nullptr;

		EnterCriticalSection(&cs); // 进入临界区

		Count++;

		if (tail)
		{
			tail->next = newNode;
		}
		else
		{
			head = newNode;
		}

		tail = newNode;


		std::string str = "Enqueue  ";
		str += std::to_string(Count);
		str += "\n";

		DWORD bytesWritten;
		WriteConsoleA(g_hConsole, str.c_str(), static_cast<DWORD>(str.length()), &bytesWritten, nullptr);

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

			Count--;
		}

		if (head == nullptr)
		{
			tail = nullptr;
		}

		std::string str = "Dequeue  ";
		str += std::to_string(Count);
		str += "\n";

		DWORD bytesWritten;
		WriteConsoleA(g_hConsole, str.c_str(), static_cast<DWORD>(str.length()), &bytesWritten, nullptr);

		LeaveCriticalSection(&cs); // 离开临界区

		return data; // 队列为空
	}
};