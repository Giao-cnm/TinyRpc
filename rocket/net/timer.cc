#include<sys/timerfd.h>
#include"rocket/net/timer.h"
#include"rocket/common/log.h"
#include"rocket/common/util.h"
#include<string.h>


namespace rocket{
    /*
    创建一个timerfd对象，并绑定相应的回调函数
    */
    Timer::Timer():FdEvent(){
        m_fd = timerfd_create(CLOCK_MONOTONIC,TFD_NONBLOCK | TFD_CLOEXEC); 
        DEBUGLOG("timer fd%d",m_fd);
        //fd可读事件绑定到了eventloop上
        listen(FdEvent::IN_EVENT,std::bind(&Timer::onTimer,this));

    }
    Timer::~Timer()
    {

    }
     void Timer::onTimer()
    {
        //水平触发，所以需要读完,处理缓存区事件，防止下一次触发可读事件
        DEBUGLOG("ontimer");
        char buf[8];
        while(1)
        {
            if((read(m_fd,buf,8)==-1)&& errno ==EAGAIN) 
            {
                break;
            }
        }
        //执行定时任务
        int64_t now = getNowMs();
        std::vector<TimerEvent::s_ptr> tmps;
        std::vector<std::pair<int64_t,std::function<void()>>> tasks;  //需要执行的任务
        ScopeMutex<Mutex> lock(m_mutex);
        auto it = m_pending_events.begin();
        for(it = m_pending_events.begin();it!=m_pending_events.end();++it)
        {
            if((*it).first <= now) 
            {
                if(!(*it).second->isCancle()){
                tmps.push_back((*it).second);
                tasks.push_back(std::make_pair((*it).second->getArriveTime(),(*it).second->getCallback()));
                }
            }else{
                break;
            }
        }
        m_pending_events.erase(m_pending_events.begin(),it);
        lock.unlock();
        //将重复的event 再次添加
        for(auto i = tmps.begin();i!=tmps.end();++i)
        {
            if((*i)->isRepeated())
            {
                (*i)->resetArriveTime();
                addTimerEvent(*i);
            }
        }
        resetArriveTime();
        for(auto i:tasks)
        {
            if(i.second) {
            i.second();
            }
        }
    }
    void Timer::resetArriveTime()
    {
        ScopeMutex<Mutex> lock(m_mutex);
        auto tmp = m_pending_events;
        lock.unlock();
        if(tmp.size()==0)
        {
            return;
        }
        int64_t now = getNowMs();

        auto it = tmp.begin();

        int64_t inteval = 0;
  
        if(it->second->getArriveTime()>now)
        {
            inteval = it->second->getArriveTime() -now;
        }else{
            inteval = 100;
        }
        timespec ts;
        memset(&ts,0,sizeof(ts));
        ts.tv_sec = inteval/1000;
        ts.tv_nsec = (inteval%1000)*1000000;
        itimerspec value;
        memset(&value,0,sizeof(value));
        value.it_value = ts;

        int rt = timerfd_settime(m_fd,0,&value,NULL);
        if (rt!=0)
        {
            ERRORLOG("timerfd_setting error, errno = %d,error = %s",errno,strerror(errno));
        }
        DEBUGLOG("timer reset to %lld",now+inteval);
        
    }
    void Timer::addTimerEvent(TimerEvent::s_ptr event)
    {
        bool is_reset_timerfd = false;
        ScopeMutex<Mutex> lock(m_mutex);

    
        if(m_pending_events.empty())
        {
            is_reset_timerfd = true;
        }else{
            auto it = m_pending_events.begin();
            if((*it).second->getArriveTime()> event->getArriveTime())
            {
                is_reset_timerfd = true;
            }
        }
        m_pending_events.emplace(event->getArriveTime(),event);
        lock.unlock();
        if(is_reset_timerfd)
        {
            resetArriveTime();
        }
    }
    void  Timer::deleteTimerEvent(TimerEvent::s_ptr event){
        event->setCancled(true);
        ScopeMutex<Mutex> lock(m_mutex);
        auto begin = m_pending_events.lower_bound(event->getArriveTime());
        auto end = m_pending_events.upper_bound(event->getArriveTime());//确定范围
        auto it = begin;
        for(it = begin;it!=end;++it)
        {
            if(it->second==event) //如果找到这个event对象
                break;
        }
        if(it!=end) m_pending_events.erase(it);
        lock.unlock();
        DEBUGLOG("success delete TimerEvent at arrive time %lld",event->getArriveTime());
    }
   
}