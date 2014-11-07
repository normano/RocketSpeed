// AUTOGENERATED FILE - DO NOT MODIFY!
// This file generated by Djinni from rocketspeed.djinni

package org.rocketspeed;

/**
 *
 * Indicates whether to subscribe or unsubscribe
 * from the specified topic.
 * Messages after the specified sequence number will be 
 * delivered to the client.
 * A seqno of 0 indicates that the subscriber is interested in
 * receiving whatever data is available via this topic.
 * A seqno of 1 indicates that the subscriber is interested in
 * receiving all the data that was published to this topic.
 *
 */
public final class SubscriptionPair {


    /*package*/ final SequenceNumber mSeqno;

    /*package*/ final Topic mTopicName;

    /*package*/ final NamespaceID mNamespaceId;

    public SubscriptionPair(
            SequenceNumber seqno,
            Topic topicName,
            NamespaceID namespaceId) {
        this.mSeqno = seqno;
        this.mTopicName = topicName;
        this.mNamespaceId = namespaceId;
    }

    public SequenceNumber getSeqno() {
        return mSeqno;
    }

    public Topic getTopicName() {
        return mTopicName;
    }

    public NamespaceID getNamespaceId() {
        return mNamespaceId;
    }
}