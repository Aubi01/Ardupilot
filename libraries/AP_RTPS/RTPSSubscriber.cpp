/*
 * Copyright (C) 2016 Intel Corporation. All rights reserved.
 *
 * Based on a file auto-generated by the fastcdrgen tool with the following
 * Copyright and license:
 * Copyright (C) 2016 Proyectos y Sistemas de Mantenimiento SL (eProsima).
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <fastrtps/attributes/ParticipantAttributes.h>
#include <fastrtps/attributes/SubscriberAttributes.h>
#include <fastrtps/Domain.h>
#include <fastrtps/participant/Participant.h>
#include <fastrtps/subscriber/SampleInfo.h>
#include <fastrtps/subscriber/Subscriber.h>

#include "RTPSSubscriber.h"

using namespace eprosima::fastrtps;

template <class State>
RTPSIsolatedSubscriber<State>::~RTPSIsolatedSubscriber()
{
    Domain::removeParticipant(participant);
    delete type;
}

template <class State>
bool RTPSIsolatedSubscriber<State>::init(const std::string& subscriber_name,
    const std::string& topic, std::function<void(State)> on_new_message_)
{
    try {
        ParticipantAttributes part_attr;
        part_attr.rtps.builtin.domainId = 0;
        part_attr.rtps.builtin.leaseDuration = c_TimeInfinite;
        part_attr.rtps.setName(subscriber_name.c_str());
        participant = Domain::createParticipant(part_attr);
        if (!participant) {
            return false;
        }

        type = new RTPSPubSubType<State>(topic);
        Domain::registerType(participant, (TopicDataType*)type);

        SubscriberAttributes sub_param;
        sub_param.topic.topicKind = NO_KEY;
        sub_param.topic.topicDataType = type->getName();
        sub_param.topic.topicName = topic.c_str();
        subscriber = Domain::createSubscriber(participant, sub_param,
            (SubscriberListener*)&listener);
        if (!subscriber) {
            return false;
        }

        listener.on_new_message = on_new_message_;
    } catch (...) {
        return false;
    }

    return true;
}

template <class State>
void RTPSIsolatedSubscriber<State>::SubListener::onSubscriptionMatched(Subscriber* sub, MatchingInfo& info)
{
    if (info.status == MATCHED_MATCHING) {
        n_matched++;
    } else {
        n_matched--;
    }
}

template <class State>
void RTPSIsolatedSubscriber<State>::SubListener::onNewDataMessage(Subscriber* sub)
{
    try {
        State st;
        eprosima::fastrtps::SampleInfo_t info;

        if (n_matched <= 0) {
            return;
        }

        if (sub->takeNextData(&st, &info)) {
            if (info.sampleKind == ALIVE) {
                on_new_message(st);
            }
        }
    } catch (...) {
    }
}

#include "AHRS.h"

template class RTPSIsolatedSubscriber<AHRS>;
