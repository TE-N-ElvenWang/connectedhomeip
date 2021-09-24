package com.google.chip.chiptool.clusterclient

import android.os.Bundle
import android.util.Log
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.SeekBar
import android.widget.Toast
import androidx.fragment.app.Fragment
import chip.devicecontroller.ChipClusters
import chip.devicecontroller.ChipClusters.ThermostatCluster
import chip.devicecontroller.ChipDeviceController
import com.google.chip.chiptool.ChipClient
import com.google.chip.chiptool.GenericChipDeviceListener
import com.google.chip.chiptool.R
import com.google.chip.chiptool.util.DeviceIdUtil
import kotlinx.android.synthetic.main.thermo_client_fragment.*
import kotlinx.android.synthetic.main.thermo_client_fragment.view.readThermoWeeklyBtn
import kotlinx.android.synthetic.main.thermo_client_fragment.view.setThermoWeeklyBtn
import kotlinx.android.synthetic.main.thermo_client_fragment.view.ReadLocalTemperatureBtn
import kotlinx.android.synthetic.main.thermo_client_fragment.view.updateAddressBtn
import kotlinx.coroutines.*

class ThermoClientFragment : Fragment() {
  private val deviceController: ChipDeviceController
    get() = ChipClient.getDeviceController(requireContext())

  private val scope = CoroutineScope(Dispatchers.Main + Job())

  override fun onCreateView(
    inflater: LayoutInflater,
    container: ViewGroup?,
    savedInstanceState: Bundle?
  ): View {
    return inflater.inflate(R.layout.thermo_client_fragment, container, false).apply {
      deviceController.setCompletionListener(ChipControllerCallback())

      updateAddressBtn.setOnClickListener { updateAddressClick() }
//      toggleBtn.setOnClickListener { scope.launch { sendToggleCommandClick() } }

      readThermoWeeklyBtn.setOnClickListener { scope.launch { sendReadCommandClick() } }
      setThermoWeeklyBtn.setOnClickListener { scope.launch { sendSetCommandClick() } }
      ReadLocalTemperatureBtn.setOnClickListener { scope.launch { sendReadLocalTemperatureClick() } }
    }
  }

  private suspend fun sendReadLocalTemperatureClick() {
    getThermostatClusterForDevice().readLocalTemperatureAttribute(object : ChipClusters.IntegerAttributeCallback {
      override fun onSuccess(value: Int) {
        Log.v(TAG, "Local Temperature attribute value: $value")
        showMessage("Local Temperature attribute value: $value")
      }

      override fun onError(ex: Exception) {
        Log.e(TAG, "Error reading Local Temperature attribute", ex)
      }
    })
  }

  override fun onStart() {
    super.onStart()
    // TODO: use the fabric ID that was used to commission the device
    val testFabricId = "5544332211"
    fabricIdEd.setText(testFabricId)
    deviceIdEd.setText(DeviceIdUtil.getLastDeviceId(requireContext()).toString())
  }

  inner class ChipControllerCallback : GenericChipDeviceListener() {
    override fun onConnectDeviceComplete() {}

    override fun onCommissioningComplete(nodeId: Long, errorCode: Int) {
      Log.d(TAG, "on CommissioningComplete for nodeId $nodeId: $errorCode")
    }

    override fun onSendMessageComplete(message: String?) {
      commandStatusTv.text = requireContext().getString(R.string.echo_status_response, message)
    }

    override fun onNotifyChipConnectionClosed() {
      Log.d(TAG, "on NotifyChipConnectionClosed")
    }

    override fun onCloseBleComplete() {
      Log.d(TAG, "on CloseBleComplete")
    }

    override fun onError(error: Throwable?) {
      Log.d(TAG, "on Error: $error")
    }
  }

  override fun onStop() {
    super.onStop()
    scope.cancel()
  }

  private suspend fun sendReadCommandClick() {
    val daysToReturn = 1
    val modeToReturn = 1
    getThermostatClusterForDevice().getWeeklySchedule(object : ChipClusters.DefaultClusterCallback {
      override fun onSuccess() {
        showMessage("get WeeklySchedule command success")
      }

      override fun onError(ex: Exception) {
        showMessage("get WeeklySchedule command failure $ex")
        Log.e(TAG, "get WeeklySchedule command failure", ex)
      }
    },daysToReturn, modeToReturn)
  }

//  private suspend fun sendSetCommandClick() {
//    val numberOfTransitionsForSequence = 0
//    val dayOfWeekForSequence = 0
//    val modeForSequence = 0
//    val payload = 0
//    getThermostatClusterForDevice().setWeeklySchedule(object : ChipClusters.DefaultClusterCallback {
//      override fun onSuccess() {
//        showMessage("set WeeklySchedule command success")
//      }
//
//      override fun onError(ex: Exception) {
//        showMessage("set WeeklySchedule command failure $ex")
//        Log.e(TAG, "set WeeklySchedule command failure", ex)
//      }
//    }, numberOfTransitionsForSequence, dayOfWeekForSequence, modeForSequence, payload)
//  }

  private suspend fun sendSetCommandClick() {
    val mode = 1
    val amount = 10
    getThermostatClusterForDevice().setpointRaiseLower(object : ChipClusters.DefaultClusterCallback {
      override fun onSuccess() {
        showMessage("set pointRaiseLower command success")
      }

      override fun onError(ex: Exception) {
        showMessage("set pointRaiseLower command failure $ex")
        Log.e(TAG, "set pointRaiseLower command failure", ex)
      }
    }, mode, amount)
  }

  private fun updateAddressClick() {
    try{
      deviceController.updateDevice(
          fabricIdEd.text.toString().toULong().toLong(),
          deviceIdEd.text.toString().toULong().toLong()
      )
      showMessage("Address update started")
    } catch (ex: Exception) {
      showMessage("Address update failed: $ex")
    }
  }

//  private suspend fun sendToggleCommandClick() {
//    getThermostatClusterForDevice().toggle(object : ChipClusters.DefaultClusterCallback {
//      override fun onSuccess() {
//        showMessage("TOGGLE command success")
//      }
//
//      override fun onError(ex: Exception) {
//        showMessage("TOGGLE command failure $ex")
//        Log.e(TAG, "TOGGLE command failure", ex)
//      }
//    })
//  }

  private suspend fun getThermostatClusterForDevice(): ThermostatCluster {
    return ThermostatCluster(
      ChipClient.getConnectedDevicePointer(requireContext(), deviceIdEd.text.toString().toLong()), 1
    )
  }

  private fun showMessage(msg: String) {
    requireActivity().runOnUiThread {
      commandStatusTv.text = msg
    }
  }

  companion object {
    private const val TAG = "ThermoClientFragment"
    fun newInstance(): ThermoClientFragment = ThermoClientFragment()
  }
}
