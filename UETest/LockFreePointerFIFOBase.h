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

	CRITICAL_SECTION cs; // ����һ�� Critical Section

public:
	FLockFreePointerFIFOBase() : head(nullptr), tail(nullptr) 
	{
		InitializeCriticalSection(&cs); // ��ʼ���ٽ���
	}

	~FLockFreePointerFIFOBase()
	{
		while (Node* node = head)
		{
			head = node->next;
			delete node->data;
			delete node;
		}

		DeleteCriticalSection(&cs); // �����ٽ���
	}

	void Enqueue(T* data)
	{
		Node* newNode = new Node(data);
		newNode->next = nullptr;

		EnterCriticalSection(&cs); // �����ٽ���

		if (tail)
		{
			tail->next = newNode;
		}
		else
		{
			head = newNode;
		}

		tail = newNode;

		LeaveCriticalSection(&cs); // �뿪�ٽ���
	}

	T* Dequeue()
	{
		T* data = nullptr;

		EnterCriticalSection(&cs); // �����ٽ���

		if (head)
		{
			data = head->data;
			head = head->next;
		}

		if (head == nullptr)
		{
			tail = nullptr;
		}

		LeaveCriticalSection(&cs); // �뿪�ٽ���

		return data; // ����Ϊ��
	}
};