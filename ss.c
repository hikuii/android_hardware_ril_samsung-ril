/*
 * This file is part of Samsung-RIL.
 *
 * Copyright (C) 2013 Paul Kocialkowski <contact@paulk.fr>
 * Copyright (C) 2017 Wolfgang Wiedmeyer <wolfgit@wiedmeyer.de>
 *
 * Samsung-RIL is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Samsung-RIL is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Samsung-RIL.  If not, see <http://www.gnu.org/licenses/>.
 */

#define LOG_TAG "RIL-SS"
#include <utils/Log.h>

#include <samsung-ril.h>
#include <utils.h>

unsigned char global_ussd_state;

int ipc_ss_ussd_callback(struct ipc_message *message)
{
	struct ipc_gen_phone_res_data *data;
	int rc;

	if (message == NULL || message->data == NULL || message->size < sizeof(struct ipc_gen_phone_res_data))
		return -1;
	
	data = (struct ipc_gen_phone_res_data *) message->data;

	rc = ipc_gen_phone_res_check(data);
	if (rc < 0) {
		RIL_LOGE("There was an error, aborting USSD request");
		goto error;
	}

	rc = ril_request_complete(ipc_fmt_request_token(message->aseq), RIL_E_SUCCESS, NULL, 0);
	if (rc < 0)
		goto error;

	global_ussd_state = 0;
	goto complete;

error:
	ril_request_complete(ipc_fmt_request_token(message->aseq), RIL_E_GENERIC_FAILURE, NULL, 0);

complete:
	return 0;
}

int ril_request_send_ussd(void *data, size_t size, RIL_Token token)
{
	char *data_enc = NULL;
	int data_enc_len = 0;
	char *message =NULL;
	struct ipc_ss_ussd_header *ussd = NULL;
	int message_size = 0xc0;
	int rc;

	if (data == NULL || size < sizeof(char *))
		goto error;

	rc = ril_radio_state_check(RADIO_STATE_SIM_NOT_READY);
	if (rc < 0)
		return RIL_REQUEST_UNHANDLED;

	switch (global_ussd_state) {
		case 0:
		case IPC_SS_USSD_NO_ACTION_REQUIRE:
		case IPC_SS_USSD_TERMINATED_BY_NET:
		case IPC_SS_USSD_OTHER_CLIENT:
		case IPC_SS_USSD_NOT_SUPPORT:
		case IPC_SS_USSD_TIME_OUT:
			RIL_LOGD("USSD Tx encoding is GSM7");

			data_enc_len = ascii2gsm7_ussd(data, (unsigned char**)&data_enc, (int) size);
			if (data_enc_len > message_size) {
				RIL_LOGE("USSD message size is too long, aborting");
				ril_request_complete(token, RIL_E_GENERIC_FAILURE, NULL, 0);

				free(data_enc);

				return RIL_REQUEST_COMPLETED;
			}

			message = malloc(message_size);
			memset(message, 0, message_size);

			ussd = (struct ipc_ss_ussd_header *) message;
			ussd->state = IPC_SS_USSD_NO_ACTION_REQUIRE;
			ussd->dcs = 0x0f; // GSM7 in that case
			ussd->length = data_enc_len;

			memcpy((void *) (message + sizeof(struct ipc_ss_ussd_header)), data_enc, data_enc_len);

			free(data_enc);

			break;
		case IPC_SS_USSD_ACTION_REQUIRE:
		default:
			RIL_LOGD("USSD Tx encoding is ASCII");

			data_enc_len = asprintf(&data_enc, "%s", (char*)data);

			if (data_enc_len > message_size) {
				RIL_LOGE("USSD message size is too long, aborting");
				ril_request_complete(token, RIL_E_GENERIC_FAILURE, NULL, 0);

				free(data_enc);

				return RIL_REQUEST_COMPLETED;
			}

			message = malloc(message_size);
			memset(message, 0, message_size);

			ussd = (struct ipc_ss_ussd_header *) message;
			ussd->state = IPC_SS_USSD_ACTION_REQUIRE;
			ussd->dcs = 0x0f; // ASCII in that case
			ussd->length = data_enc_len;

			memcpy((void *) (message + sizeof(struct ipc_ss_ussd_header)), data_enc, data_enc_len);

			free(data_enc);

			break;
	}

	if (message == NULL) {
		RIL_LOGE("USSD message is empty, aborting");

		ril_request_complete(token, RIL_E_GENERIC_FAILURE, NULL, 0);
		return RIL_REQUEST_COMPLETED;
	}

	ipc_gen_phone_res_expect_callback(ipc_fmt_request_seq(token), IPC_SS_USSD,
		ipc_ss_ussd_callback);

	rc = ipc_fmt_send(ipc_fmt_request_seq(token), IPC_SS_USSD, IPC_TYPE_EXEC, (void *) message, message_size);
	if (rc < 0)
		goto error;

	rc = RIL_REQUEST_HANDLED;
	goto complete;

error:
	ril_request_complete(token, RIL_E_GENERIC_FAILURE, NULL, 0);
	return RIL_REQUEST_COMPLETED;
complete:
	return rc;
}

int ril_request_cancel_ussd(void *data, size_t size, RIL_Token token)
{
	struct ipc_ss_ussd_header ussd;
	int rc;

	rc = ril_radio_state_check(RADIO_STATE_SIM_READY);
	if (rc < 0)
		return RIL_REQUEST_UNHANDLED;

	memset(&ussd, 0, sizeof(ussd));

	ussd.state = IPC_SS_USSD_TERMINATED_BY_NET;
	global_ussd_state = IPC_SS_USSD_TERMINATED_BY_NET;

	rc = ipc_gen_phone_res_expect_complete(ipc_fmt_request_seq(token), IPC_SS_USSD);
	if (rc < 0)
		goto error;

	rc = ipc_fmt_send(ipc_fmt_request_seq(token), IPC_SS_USSD, IPC_TYPE_EXEC, (void *) &ussd, sizeof(ussd));
	if (rc < 0)
		goto error;

	rc = RIL_REQUEST_HANDLED;
	goto complete;

error:
	ril_request_complete(token, RIL_E_GENERIC_FAILURE, NULL, 0);
	rc = RIL_REQUEST_COMPLETED;

complete:
	return rc;
}

void ipc2ril_ussd_state(struct ipc_ss_ussd_header *ussd, char *message[2])
{
	if (ussd == NULL || message == NULL)
		return;

	switch (ussd->state) {
		case IPC_SS_USSD_NO_ACTION_REQUIRE:
			asprintf(&message[0], "%d", 0);
			break;
		case IPC_SS_USSD_ACTION_REQUIRE:
			asprintf(&message[0], "%d", 1);
			break;
		case IPC_SS_USSD_TERMINATED_BY_NET:
			asprintf(&message[0], "%d", 2);
			break;
		case IPC_SS_USSD_OTHER_CLIENT:
			asprintf(&message[0], "%d", 3);
			break;
		case IPC_SS_USSD_NOT_SUPPORT:
			asprintf(&message[0], "%d", 4);
			break;
		case IPC_SS_USSD_TIME_OUT:
			asprintf(&message[0], "%d", 5);
			break;
	}
}

int ipc_ss_ussd(struct ipc_message *message)
{
	char *data_dec = NULL;
	int data_dec_len = 0;
	sms_coding_scheme coding_scheme;

	char *ussd_message[2];

	struct ipc_ss_ussd_header *ussd = NULL;
	unsigned char state;

	if (message == NULL || message->data == NULL || message->size < sizeof(struct ipc_ss_ussd_header))
		goto error;

	memset(ussd_message, 0, sizeof(ussd_message));

	ussd = (struct ipc_ss_ussd_header *) message->data;

	ipc2ril_ussd_state(ussd, ussd_message);

	global_ussd_state = ussd->state;

	if (ussd->length > 0 && message->size > 0 && message->data != NULL) {
		coding_scheme = sms_get_coding_scheme(ussd->dcs);
		switch (coding_scheme) {
			case SMS_CODING_SCHEME_GSM7:
				RIL_LOGD("USSD Rx encoding is GSM7");

				data_dec_len = gsm72ascii((unsigned char *) message->data
							  + sizeof(struct ipc_ss_ussd_header), &data_dec, message->size - sizeof(struct ipc_ss_ussd_header));
				asprintf(&ussd_message[1], "%s", data_dec);
				ussd_message[1][data_dec_len] = '\0';

				break;
			case SMS_CODING_SCHEME_UCS2:
				RIL_LOGD("USSD Rx encoding %x is UCS2", ussd->dcs);

				data_dec_len = message->size - sizeof(struct ipc_ss_ussd_header);
				ussd_message[1] = malloc(data_dec_len * 4 + 1);

				int i, result = 0;
				char *ucs2 = (char*)message->data + sizeof(struct ipc_ss_ussd_header);
				for (i = 0; i < data_dec_len; i += 2) {
					int c = (ucs2[i] << 8) | ucs2[1 + i];
					result += utf8_write(ussd_message[1], result, c);
				}
				ussd_message[1][result] = '\0';
				break;
			default:
				RIL_LOGD("USSD Rx encoding %x is unknown, assuming ASCII",
					ussd->dcs);

				data_dec_len = message->size - sizeof(struct ipc_ss_ussd_header);
				asprintf(&ussd_message[1], "%s", (unsigned char *) message->data + sizeof(struct ipc_ss_ussd_header));
				ussd_message[1][data_dec_len] = '\0';
				break;
		}
	}

	ril_request_unsolicited(RIL_UNSOL_ON_USSD, ussd_message, sizeof(ussd_message));

	return 0;

error:
	ril_request_complete(ipc_fmt_request_token(message->aseq), RIL_E_GENERIC_FAILURE, NULL, 0);
	return 0;
}
