#pragma once

#include <entt/entt.hpp>

namespace vultra
{
    class EventCenter
    {
    public:
        static EventCenter& get()
        {
            static EventCenter instance;
            return instance;
        }

        EventCenter(const EventCenter&)            = delete;
        EventCenter& operator=(const EventCenter&) = delete;

        template<typename Event, typename Class, auto Method>
        void subscribe(Class& instance)
        {
            m_Dispatcher.sink<Event>().template connect<Method>(instance);
        }

        template<typename Event, typename Class, auto Method>
        void unsubscribe(Class& instance)
        {
            m_Dispatcher.sink<Event>().template disconnect<Method>(instance);
        }

        template<typename Event, typename... Args>
        void emit(Args&&... args)
        {
            m_Dispatcher.enqueue<Event>(std::forward<Args>(args)...);
        }

        void update() { m_Dispatcher.update(); }

    private:
        EventCenter()  = default;
        ~EventCenter() = default;

        entt::dispatcher m_Dispatcher;
    };
} // namespace vultra

#define VULTRA_EVENT_SUBSCRIBE(EventType, ClassType, Method) \
    ::vultra::EventCenter::get().subscribe<EventType, ClassType, &ClassType::Method>(*this);
#define VULTRA_EVENT_SUBSCRIBE_INSTANCE(EventType, ClassType, Method, Instance) \
    ::vultra::EventCenter::get().subscribe<EventType, ClassType, &ClassType::Method>(Instance);
#define VULTRA_EVENT_UNSUBSCRIBE(EventType, ClassType, Method) \
    ::vultra::EventCenter::get().unsubscribe<EventType, ClassType, &ClassType::Method>(*this);
#define VULTRA_EVENT_UNSUBSCRIBE_INSTANCE(EventType, ClassType, Method, Instance) \
    ::vultra::EventCenter::get().unsubscribe<EventType, ClassType, &ClassType::Method>(Instance);

#define VULTRA_EVENT_EMIT(EventType, ...) ::vultra::EventCenter::get().emit<EventType>(__VA_ARGS__);
#define VULTRA_EVENT_UPDATE() ::vultra::EventCenter::get().update();
#define VULTRA_EVENT_EMIT_NOW(EventType, ...) \
    do \
    { \
        VULTRA_EVENT_EMIT(EventType, __VA_ARGS__); \
        VULTRA_EVENT_UPDATE(); \
    } while (0)