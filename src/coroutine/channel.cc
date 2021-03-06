/*
  +----------------------------------------------------------------------+
  | Swoole                                                               |
  +----------------------------------------------------------------------+
  | This source file is subject to version 2.0 of the Apache license,    |
  | that is bundled with this package in the file LICENSE, and is        |
  | available through the world-wide-web at the following url:           |
  | http://www.apache.org/licenses/LICENSE-2.0.html                      |
  | If you did not receive a copy of the Apache2.0 license and are unable|
  | to obtain it through the world-wide-web, please send a note to       |
  | license@swoole.com so we can mail you a copy immediately.            |
  +----------------------------------------------------------------------+
  | Author: Tianfeng Han  <mikan.tenny@gmail.com>                        |
  +----------------------------------------------------------------------+
*/

#include "channel.h"

#include <unordered_map>

using namespace swoole;

static void channel_pop_timeout(swTimer *timer, swTimer_node *tnode)
{
    timeout_msg_t *msg = (timeout_msg_t *) tnode->data;
    msg->error = true;
    msg->timer = nullptr;
    msg->chan->remove(msg->co);
    coroutine_resume(msg->co);
}

Channel::Channel(size_t _capacity)
{
    capacity = _capacity;
    closed = false;
}

void Channel::yield(enum channel_op type)
{
    int _cid = coroutine_get_current_cid();
    if (_cid == -1)
    {
        swError("Channel::yield() must be called in the coroutine.");
    }
    coroutine_t *co = coroutine_get_by_id(_cid);
    if (type == PRODUCER)
    {
        producer_queue.push_back(co);
        swDebug("producer[%d]", coroutine_get_cid(co));
    }
    else
    {
        consumer_queue.push_back(co);
        swDebug("consumer[%d]", coroutine_get_cid(co));
    }
    coroutine_yield(co);
}

void* Channel::pop(double timeout)
{
    if (closed)
    {
        return nullptr;
    }
    timeout_msg_t msg;
    msg.error = false;
    if (timeout > 0)
    {
        int msec = (int) (timeout * 1000);
        msg.chan = this;
        msg.co = coroutine_get_by_id(coroutine_get_current_cid());
        msg.timer = swTimer_add(&SwooleG.timer, msec, 0, &msg, channel_pop_timeout);
    }
    else
    {
        msg.timer = NULL;
    }
    if (is_empty() || consumer_queue.size() > 0)
    {
        yield(CONSUMER);
    }
    if (msg.timer)
    {
        swTimer_del(&SwooleG.timer, msg.timer);
    }
    if (msg.error || closed || data_queue.size() == 0)
    {
        return nullptr;
    }
    /**
     * pop data
     */
    void *data = data_queue.front();
    data_queue.pop();
    /**
     * notify producer
     */
    if (producer_queue.size() > 0)
    {
        coroutine_t *co = pop_coroutine(PRODUCER);
        coroutine_resume(co);
    }
    return data;
}

bool Channel::push(void *data)
{
    if (closed)
    {
        return false;
    }
    if (is_full() || producer_queue.size() > 0)
    {
        yield(PRODUCER);
    }
    if (closed)
    {
        return false;
    }
    /**
     * push data
     */
    data_queue.push(data);
    swDebug("push data, count=%ld", length());
    /**
     * notify consumer
     */
    if (consumer_queue.size() > 0)
    {
        coroutine_t *co = pop_coroutine(CONSUMER);
        coroutine_resume(co);
    }
    return true;
}

bool Channel::close()
{
    if (closed)
    {
        return false;
    }
    swDebug("closed");
    closed = true;
    while (producer_queue.size() > 0)
    {
        coroutine_t *co = pop_coroutine(PRODUCER);
        coroutine_resume(co);
    }
    while (consumer_queue.size() > 0)
    {
        coroutine_t *co = pop_coroutine(CONSUMER);
        coroutine_resume(co);
    }
    return true;
}
