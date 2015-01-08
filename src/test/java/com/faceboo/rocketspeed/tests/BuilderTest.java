package com.faceboo.rocketspeed.tests;

import org.rocketspeed.Builder;
import org.rocketspeed.MessageReceived;
import org.rocketspeed.MsgId;
import org.rocketspeed.PublishCallback;
import org.rocketspeed.ReceiveCallback;
import org.rocketspeed.Status;
import org.rocketspeed.SubscribeCallback;

import org.junit.Test;

/**
 * This test is deliberately placed in a separate package, so that we can verify visibility of user
 * facing types.
 */
public class BuilderTest {

  @Test
  public void testBuilder() throws Exception {
    Builder builder = new Builder()
        .clientID("client-id-123")
        .publishCallback(new PublishCallback() {
          @Override
          public void call(Status status, short namespaceId, String topicName, MsgId messageId,
                           long sequenceNumber, byte[] contents) {
          }
        }).subscribeCallback(new SubscribeCallback() {
          @Override
          public void call(Status status, long sequenceNumber, boolean subscribed) {
          }
        }).receiveCallback(new ReceiveCallback() {
          @Override
          public void call(MessageReceived message) {
          }
        }).usingFileStorage("/tmp/rocketspeed-storage-file-123");
  }
}