import json
import boto3
import os
import datetime
import sys
import base64

# variables
C_SEC_CODE = '***'
C_UUID_BELL = '***'
C_SENDER_EMAIL = 'info@list.com'
C_AWS_REGION = "eu-central-1"
C_WHATSAPP_SENDER_ID = '***'
C_SMS_SENDER_ID = '***'

# Initialize SNS client
region = os.getenv("AWS_REGION", C_AWS_REGION)  # Default region
sns_client = boto3.client('sns', region_name=region)

# Initialize AWS clients
ses_client = boto3.client('ses')
socmes_client = boto3.client('socialmessaging', region_name=region)

# lists
phone_list = ['+49...', '+49...']
email_list = ['email1@list.com', 'email2@list.com']

def send_email(recipient_email, subject, body):
    sender_email = C_SENDER_EMAIL
    return ses_client.send_email(
            Source=sender_email,
            Destination={
                'ToAddresses': [recipient_email]
            },
            Message={
                'Subject': {
                    'Data': subject
                },
                'Body': {
                    'Text': {
                        'Data': body
                    }
                }
            }
        )

def send_phonemessage(sender_id, phone_number, hourminute):
    return send_phone_sms_message(sender_id, phone_number, hourminute)

def send_phone_sms_message(sender_id, phone_number, hourminute):
    message = 'Bell is ringing at ' + hourminute
    return sns_client.publish(
        PhoneNumber=phone_number, 
        Message=message,
        MessageAttributes= {
            'AWS.SNS.SMS.SenderID': {
                'DataType': 'String',
                'StringValue': sender_id
            },
            'AWS.SNS.SMS.SMSType': {
                'DataType': 'String',
                'StringValue': 'Transactional'
                }
                })

def send_phone_whatsapp_message(sender_id, phone_number, hourminute):
    body = {
        "messaging_product": "whatsapp",
        "to": phone_number,
        "type": "template",
        "template": {
            "name": "bell_notification",
            "language": {
                "code": "en_US"
            },
            "components": [
                {
                    "type": "body",
                    "parameters": [
                        {
                            "type": "text",
                            "text": hourminute
                        }
                    ]
                }
            ]
        }
    }
    json_bytes = json.dumps(body).encode('utf-8')
    base64_bytes = base64.b64encode(json_bytes)    
    return socmes_client.send_whatsapp_message(
        originationPhoneNumberId= C_WHATSAPP_SENDER_ID, 
        message= json_bytes,
        metaApiVersion="v20.0"
        )

def get_subject(action, hourminute):
    if action.upper().strip() == 'BELL':
        return '🔔 BELL 🔔 @ ' + hourminute
    else:
        return 'unknown action'

def get_message(action, dateandhour):
    if action.upper().strip() == 'BELL':
        return 'someone is ringing your Bell @ ' + dateandhour
    else:
        return 'unknown action'

def lambda_handler(event, context):
    """
    AWS Lambda function to send an SMS and/or email
    """
    print(f'python={sys.version}')
    print(f'boto3={boto3.__version__}')
    response = []
    # Extract phone number and message from the event
    try:
        query_params = event.get('queryStringParameters', {})

        # querystring arguments
        sec = query_params.get('sec') # required
        uuid = query_params.get('uuid')  # required
        action = query_params.get('action')  # required
        only_phone = query_params.get('only_phone', 'no')  # required
        sender_id = event.get('sender_id', C_SMS_SENDER_ID)

        # verifying security code
        if not sec == C_SEC_CODE:
            return {
            'statusCode': 400,
            'body': json.dumps(f"wrong auth")
        }

        # verifying action
        if not action:
            return {
            'statusCode': 400,
            'body': json.dumps(f"missing action")
        }

        # generated text
        dateandhour = datetime.datetime.now().strftime("%d.%m.%Y %H:%M:%S")
        hourminute = datetime.datetime.now().strftime("%H:%M")
        subject = get_subject(action, hourminute)
        message = get_message(action, dateandhour)

        # verifying bell uuid
        if uuid == C_UUID_BELL:
            # sms
            for phone_item in phone_list:
                # phone (e.g. sms)
                response.append(send_phonemessage(sender_id, phone_item, hourminute))
            if only_phone.lower().strip() == 'no':
                # email
                for email_item in email_list:
                    response.append(send_email(email_item, subject, message))
        else:
            response.append('unknown delivery list...')

    except KeyError as e:
        return {
            'statusCode': 400,
            'body': json.dumps(f"Missing required field: {e}")
        }
    except Exception as e:
        return {
            'statusCode': 500,
            'body': json.dumps(f"critical error: {str(e)}")
        }

    try:
        return {
            'statusCode': 200,
            'body': json.dumps(response)
        }

    except Exception as e:
        return {
            'statusCode': 500,
            'body': json.dumps(f"Error sending SMS: {str(e)}")
        }
