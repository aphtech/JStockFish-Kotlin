package [your_package_name].jstockfish

/** Listener to capture Stockfish async output. */
interface OutputListener {
    fun onOutput(output: String)
}